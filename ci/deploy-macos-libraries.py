#!/usr/bin/env python3
"""Close a macdeployqt bundle's transitive Homebrew library dependencies."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import stat
import struct
import subprocess
import sys

LOAD_COMMANDS = {0xc, 0x18, 0x1f, 0x23}


def macho(path):
    data = path.read_bytes()
    if data[:4] != b'\xcf\xfa\xed\xfe':
        return None
    if len(data) < 32:
        raise ValueError('Truncated Mach-O header: '+str(path))
    cpu = int.from_bytes(data[4:8], 'little')
    count = int.from_bytes(data[16:20], 'little')
    offset = 32
    dependencies = []
    for unused in range(count):
        if offset+8 > len(data): raise ValueError('Truncated Mach-O command')
        command, size = struct.unpack_from('<II', data, offset)
        if size < 8 or offset+size > len(data): raise ValueError('Invalid Mach-O command size')
        if command & 0x7fffffff in LOAD_COMMANDS:
            if size < 24: raise ValueError('Truncated dylib load command')
            start = int.from_bytes(data[offset+8:offset+12], 'little')
            if start < 24 or start >= size: raise ValueError('Invalid dylib name offset')
            dependencies.append(data[offset+start:offset+size].split(b'\0', 1)[0].decode())
        offset += size
    return cpu, dependencies


def system_library(name):
    return name.startswith(('/System/Library/', '/usr/lib/'))


def inside(path, root):
    return path == root or root in path.parents


def homebrew_library(prefix, name, cpu):
    prefix = prefix.resolve()
    if Path(name).name != name or not name.endswith('.dylib'):
        raise RuntimeError('Unsupported missing library name: '+name)
    direct = prefix/'lib'/name
    paths = [direct] if direct.is_file() else list(prefix.glob('opt/*/lib/'+name))
    candidates = {p.resolve() for p in paths if p.is_file()}
    if len(candidates) != 1:
        raise RuntimeError('Missing or ambiguous Homebrew library: '+name)
    path = candidates.pop()
    if not inside(path, prefix): raise RuntimeError('Homebrew symlink escapes its prefix: '+str(path))
    info = macho(path)
    if info is None or info[0] != cpu: raise RuntimeError('Wrong library architecture: '+str(path))
    return path


def relative_target(app, consumer, name):
    if name.startswith('@loader_path/'):
        return (consumer.parent/name[len('@loader_path/'):]).resolve()
    if name.startswith('@executable_path/'):
        return (app/'Contents/MacOS'/name[len('@executable_path/'):]).resolve()
    if name.startswith('@rpath/'):
        return (app/'Contents/Frameworks'/name[len('@rpath/'):]).resolve()
    if name.startswith('/'):
        return Path(name).resolve()
    raise RuntimeError('Unsupported library reference: '+name)


def deploy(app, prefix, report):
    app = app.resolve(); prefix = prefix.resolve()
    binary = app/'Contents/MacOS/Babacoin-Qt'
    main_info = macho(binary)
    if main_info is None: raise RuntimeError('Missing native app executable')
    cpu = main_info[0]
    expected = {'arm64': 0x100000c, 'x86_64': 0x1000007}[platform.machine()]
    if cpu != expected: raise RuntimeError('App architecture differs from native runner')
    frameworks = app/'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    queue = [p for p in app.rglob('*') if p.is_file() and not p.is_symlink()]
    seen = set(); added = []; rewritten = 0
    while queue:
        consumer = queue.pop().resolve()
        if consumer in seen: continue
        seen.add(consumer)
        info = macho(consumer)
        if info is None: continue
        if info[0] != cpu: raise RuntimeError('Mixed app architectures: '+str(consumer))
        changes = []
        for dependency in info[1]:
            if system_library(dependency): continue
            target = relative_target(app, consumer, dependency)
            if not target.is_file() or not inside(target, app):
                # Qt frameworks must already have been deployed with their
                # resources by macdeployqt. Never copy a lone framework binary.
                if '.framework/' in dependency:
                    parts = dependency.split('/')
                    index = next(i for i, part in enumerate(parts) if part.endswith('.framework'))
                    target = (frameworks/Path(*parts[index:])).resolve()
                    if not target.is_file() or not inside(target, app):
                        raise RuntimeError('Missing deployed Qt framework: '+dependency)
                else:
                    name = dependency.rsplit('/', 1)[-1]
                    if dependency.startswith('/'):
                        original = Path(dependency).resolve()
                        if not inside(original, prefix) or not original.is_file():
                            raise RuntimeError('Unexpected external library: '+dependency)
                        original_info = macho(original)
                        if original_info is None or original_info[0] != cpu:
                            raise RuntimeError('Wrong external library architecture')
                    else:
                        original = homebrew_library(prefix, name, cpu)
                    target = frameworks/name
                    if target.is_symlink(): raise RuntimeError('Unexpected library destination symlink')
                    if not target.exists():
                        shutil.copyfile(str(original), str(target))
                        target.chmod(target.stat().st_mode | stat.S_IWUSR)
                        subprocess.check_call(['install_name_tool', '-id', '@rpath/'+name, str(target)])
                        added.append(name)
                    queue.append(target)
                    target = target.resolve()
            if not inside(target, app): raise RuntimeError('Library escaped the app bundle')
            local = '@loader_path/'+Path(os.path.relpath(str(target), str(consumer.parent))).as_posix()
            if dependency != local: changes += ['-change', dependency, local]
        if changes:
            consumer.chmod(consumer.stat().st_mode | stat.S_IWUSR)
            subprocess.check_call(['install_name_tool']+changes+[str(consumer)])
            rewritten += 1
    checked = 0
    for path in app.rglob('*'):
        if not path.is_file() or path.is_symlink(): continue
        info = macho(path)
        if info is None: continue
        checked += 1
        for dependency in info[1]:
            if system_library(dependency): continue
            if not dependency.startswith('@loader_path/'):
                raise RuntimeError('Unresolved deployed library reference: '+dependency)
            target = relative_target(app, path, dependency)
            if not inside(target, app) or not target.is_file():
                raise RuntimeError('Deployed dependency is absent: '+dependency)
    result = dict(result='PASS', source_commit=os.environ['GITHUB_SHA'],
                  native_architecture=platform.machine(), macho_files_checked=checked,
                  added_libraries=sorted(added), rewritten_files=rewritten)
    report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result))


if __name__ == '__main__':
    if sys.platform != 'darwin' or os.environ.get('GITHUB_ACTIONS') != 'true':
        raise SystemExit('Disposable native macOS CI runner required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    deploy(args.app, args.prefix, args.report)
