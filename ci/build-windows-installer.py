#!/usr/bin/env python3
"""Wrap an already-tested Windows ZIP; never rebuild or modify its executables."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import zipfile
from release_policy import check_source


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--native-report', type=Path, required=True)
    parser.add_argument('--arm-report', type=Path, required=True)
    parser.add_argument('--binary-source', required=True)
    parser.add_argument('--packaging-source', required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    check_source(args.tag)
    for revision in (args.binary_source, args.packaging_source):
        if not re.fullmatch(r'[0-9a-f]{40}', revision):
            raise ValueError('A full source commit is required')
    if not re.fullmatch(r'v2\.0\.0(?:-rc\.[1-9][0-9]*)?', args.tag):
        raise ValueError('Only V2.0.0 and its release candidates are supported')
    native = json.loads(args.native_report.read_text(encoding='utf-8-sig'))
    arm = json.loads(args.arm_report.read_text(encoding='utf-8-sig'))
    for report in (native, arm):
        assert report['result'] == 'PASS' and report['source_commit'] == args.binary_source
        assert report['core_cases'] == 345 and report['qt_suites'] == 6
    assert arm['host_runner_architecture'].upper() in ('ARM64', 'AARCH64')
    assert native['binaries_sha256'] == arm['binaries_sha256']
    checksum = Path(str(args.payload) + '.sha256').read_text(encoding='utf-8-sig').split()[0].lower()
    assert re.fullmatch(r'[0-9a-f]{64}', checksum) and digest(args.payload) == checksum
    output = args.output.resolve()
    output.mkdir(exist_ok=False)
    payload = output / 'payload'
    payload.mkdir()
    forbidden = {'wallet.dat', 'babacoin.conf', '.cookie', 'blocks', 'chainstate',
                 'evodb', 'wallets', 'database', 'backups', 'debug.log'}
    files = {}
    seen = set()
    total = 0
    with zipfile.ZipFile(args.payload) as archive:
        assert archive.read('SOURCE_COMMIT').decode('utf-8-sig').strip() == args.binary_source
        for member in archive.infolist():
            name = PurePosixPath(member.filename)
            if (name.is_absolute() or '..' in name.parts or '\\' in member.filename
                    or ':' in member.filename or stat.S_ISLNK(member.external_attr >> 16)
                    or any(part.lower() in forbidden for part in name.parts)):
                raise ValueError('Unsafe installer payload path: ' + member.filename)
            folded = member.filename.lower().rstrip('/')
            if folded in seen:
                raise ValueError('Duplicate Windows payload path')
            seen.add(folded)
            total += member.file_size
            assert total < 2 * 1024**3 and member.file_size < 512 * 1024**2
            destination = payload.joinpath(*name.parts)
            assert payload in destination.resolve().parents or destination.resolve() == payload
            if member.is_dir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(member) as source, destination.open('xb') as target:
                shutil.copyfileobj(source, target)
            files[name.as_posix()] = digest(destination)
    for program in ('babacoind', 'babacoin-cli', 'babacoin-tx', 'babacoin-qt'):
        key = ('qt/' if program == 'babacoin-qt' else '') + program + '.exe'
        assert files[program + '.exe'] == native['binaries_sha256'][key]
    assert 'platforms/qwindows.dll' in files and 'COPYING' in files
    repo = Path(__file__).resolve().parents[1]
    subprocess.run([str(args.compiler.resolve()), '/Qp', '/DPayloadDir=' + str(payload),
                    '/DOutputDir=' + str(output), '/DReleaseTag=' + args.tag,
                    str(repo / 'contrib/windows/babacoin-setup.iss')], check=True)
    installer = output / ('babacoin-' + args.tag + '-windows-x86_64-setup.exe')
    assert installer.is_file()
    setup_hash = digest(installer)
    Path(str(installer) + '.sha256').write_text(setup_hash + '  ' + installer.name + '\n', encoding='ascii')
    manifest = {'format': 1, 'tag': args.tag, 'binary_source_commit': args.binary_source,
                'packaging_source_commit': args.packaging_source,
                'portable_archive_sha256': checksum,
                'installer': installer.name, 'installer_sha256': setup_hash,
                'compiler_version': 'Inno Setup 7.1.1 x64',
                'compiler_sha256': digest(args.compiler),
                'payload_files_sha256': files,
                'native_and_arm64_payload_tests': 'PASS',
                'wallet_files_included': False}
    (output / 'INSTALLER-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Built installer from %d verified payload files; wallet executables unchanged.' % len(files))


if __name__ == '__main__':
    main()
