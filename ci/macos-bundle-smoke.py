#!/usr/bin/env python3
"""Validate a deployed macOS app on a disposable native CI runner only."""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shlex
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'test/util'))
from isolated_smoke import Node


def retain_diagnostics(work, output):
    for source, name in [(work/'data/smoke-process.log', 'bundled-app-process.log'),
                         (work/'data/regtest/debug.log', 'bundled-app-debug.log')]:
        if source.is_file():
            data = source.read_bytes()[-2*1024*1024:]
            (output/name).write_bytes(data)
            print(name+':\n'+data[-6000:].decode(errors='replace'))


def main():
    if sys.platform != 'darwin' or os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('A disposable native macOS CI runner is required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    app = args.app.resolve()
    metadata = plistlib.loads((app/'Contents/Info.plist').read_bytes())
    assert metadata['CFBundleExecutable'] == 'Babacoin-Qt'
    assert metadata['CFBundleIconFile'] == 'babacoin.icns'
    binary = app/'Contents/MacOS/Babacoin-Qt'
    assert binary.is_file()
    subprocess.check_call(['codesign', '--verify', '--deep', '--strict', str(app)])

    # The app must resolve its bundled frameworks/plugins rather than relying
    # on an absolute Homebrew path that exists only on the build machine.
    checked = set()
    for path in app.rglob('*'):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open('rb') as stream:
            magic = stream.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe',
                          b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'):
            continue
        listing = subprocess.check_output(['otool', '-L', str(path)], text=True)
        # -L includes a dylib's own LC_ID_DYLIB as well as its dependencies.
        # A retained build-machine ID is not a runtime load dependency.
        install_ids = {line.strip() for line in
                       subprocess.check_output(['otool', '-D', str(path)], text=True).splitlines()[1:]}
        for line in listing.splitlines()[1:]:
            dependency = line.strip().split(' (', 1)[0]
            if dependency in install_ids:
                continue
            if dependency.startswith('/'):
                assert dependency.startswith(('/System/Library/', '/usr/lib/')), dependency
            else:
                assert dependency.startswith(('@rpath/', '@loader_path/', '@executable_path/')), dependency
        checked.add(str(path.relative_to(app)))
    assert checked

    source = os.environ['GITHUB_SHA']
    assert re.fullmatch('[a-f0-9]{40}', source)
    config = (Path(__file__).resolve().parents[1]/'configure.ac').read_text()
    parts = [int(re.search(r'define\(_CLIENT_VERSION_' + n + r', ([0-9]+)\)', config)[1])
             for n in ('MAJOR', 'MINOR', 'REVISION', 'BUILD')]
    version = sum(n*scale for n, scale in zip(parts, (1000000, 10000, 100, 1)))
    assert metadata['CFBundleShortVersionString'] == '.'.join(map(str, parts))
    report = dict(result='PASS', source_commit=source,
                  binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  bundled_macho_files_checked=len(checked),
                  checks=['deep strict ad hoc signature verification',
                          'no absolute external non-system library dependencies',
                          'bundled Qt platform plugin startup',
                          'fresh regtest wallet opens with zero P2P connections',
                          'owned address survives clean GUI restart'])
    with tempfile.TemporaryDirectory(prefix='bbc-bundled-app-') as temp, contextlib.ExitStack() as diagnostics:
        work = Path(temp)
        diagnostics.callback(retain_diagnostics, work, args.report.parent)
        for key in list(os.environ):
            if key.startswith(('DYLD_', 'QT_')):
                os.environ.pop(key)
        os.environ.update(QT_QPA_PLATFORM='minimal:enable_fonts', QT_STYLE_OVERRIDE='Fusion',
                          QT_PLUGIN_PATH=str(app/'Contents/PlugIns'),
                          QT_QPA_PLATFORM_PLUGIN_PATH=str(app/'Contents/PlugIns/platforms'),
                          HOME=str(work), XDG_CONFIG_HOME=str(work/'settings'))
        settings = work/'settings'
        (settings/'Babacoin').mkdir(parents=True)
        (settings/'Babacoin/Babacoin-Qt-regtest.ini').write_text(
            '[General]\nfAppearanceSetupDone=true\ntheme=Dark\n')
        # Configure only this synthetic GUI profile. An unattended smoke test
        # must not wait at the first-run appearance dialog or use runner prefs.
        launcher = work/'start-bundled-app.sh'
        launcher.write_text('#!/bin/sh\nexec ' + shlex.quote(str(binary)) +
                            ' -uiconfigdir=' + shlex.quote(str(settings)) +
                            ' -choosedatadir=0 -explorerpeers=0 -enablecoinjoin=0 -walletbroadcast=0 "$@"\n')
        launcher.chmod(0o700)
        with Node(launcher, work/'data', True) as node:
            network = node.rpc('getnetworkinfo')
            assert network['version'] == version
            assert 'pre-release test build' not in network.get('warnings', '')
            assert node.rpc('getblockchaininfo')['chain'] == 'regtest'
            assert node.rpc('getconnectioncount') == 0
            address = node.rpc('getnewaddress')
            assert node.rpc('getaddressinfo', address)['ismine']
            report.update(version=version, subversion=network['subversion'])
        assert node.process.returncode == 0
        with Node(launcher, work/'data', True) as node:
            assert node.rpc('getaddressinfo', address)['ismine']
            assert node.rpc('getconnectioncount') == 0
        assert node.process.returncode == 0
    args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
