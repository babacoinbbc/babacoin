#!/usr/bin/env python3
"""Install/uninstall only in fresh synthetic directories; never open a real wallet."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installer', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    # The product AppId owns per-user uninstall/shortcut metadata even when
    # /DIR points at a temporary folder. Never run this harness over a real
    # installation; CI supplies a clean Windows user profile.
    import winreg
    if (Path(os.environ['LOCALAPPDATA']) / 'Programs/Babacoin Core/babacoin-qt.exe').exists():
        raise RuntimeError('Installer tests require a clean Windows user profile; an existing Babacoin program was found')
    for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
        for key in (r'Software\Microsoft\Windows\CurrentVersion\Uninstall\{C40A2BB5-A761-47E2-A872-284A9E6BBC02}_is1',
                    r'Software\Babacoin\Installer'):
            try:
                installed = winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, 0, winreg.KEY_READ | view)
            except FileNotFoundError:
                continue
            installed.Close()
            raise RuntimeError('Installer tests require a clean Windows user profile; an existing Babacoin installation was found')
    manifest = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    assert digest(args.installer) == manifest['installer_sha256']
    root = Path(tempfile.mkdtemp(prefix='babacoin-installer-test-')).resolve()
    app = root / 'program'
    wallet = root / 'wallet data'
    wallet.mkdir()
    sentinel = b'SYNTHETIC PRESERVATION FIXTURE - NOT A WALLET\n'
    (wallet / 'wallet.dat').write_bytes(sentinel)
    (wallet / 'babacoin.conf').write_text('# synthetic configuration\n', encoding='ascii')
    wallet_hash = digest(wallet / 'wallet.dat')
    tests = []

    def install(directory, label):
        assert root in directory.resolve().parents
        return subprocess.run([str(args.installer.resolve()), '/VERYSILENT', '/SUPPRESSMSGBOXES',
                               '/NORESTART', '/SP-', '/NOICONS', '/TASKS=',
                               '/DIR=' + str(directory), '/WALLETDIR=' + str(wallet),
                               '/LOG=' + str(root / (label + '.log'))], timeout=180).returncode

    # Silent mode must not bypass wallet-directory protection.
    assert install(wallet, 'reject-data-folder') != 0
    assert digest(wallet / 'wallet.dat') == wallet_hash
    assert not (wallet / 'babacoin-qt.exe').exists()
    tests.append('wallet-directory installation rejected')
    assert install(app, 'install') == 0

    def verify_payload():
        for name, expected in manifest['payload_files_sha256'].items():
            assert digest(app / name) == expected, name
        assert digest(wallet / 'wallet.dat') == wallet_hash

    verify_payload()
    tests.append('all installed payload hashes match tested ZIP')
    assert install(app, 'reinstall') == 0
    verify_payload()
    tests.append('reinstall preserves external wallet bytes')

    env = os.environ.copy()
    system = Path(os.environ['SystemRoot'])
    env['PATH'] = os.pathsep.join((str(app), str(system / 'System32'), str(system)))
    env['QT_QPA_PLATFORM_PLUGIN_PATH'] = str(app / 'platforms')
    env['QT_PLUGIN_PATH'] = str(app)
    env['QT_QPA_PLATFORM'] = 'minimal:enable_fonts'
    env['QT_STYLE_OVERRIDE'] = 'Fusion'
    version = subprocess.check_output([str(app / 'babacoind.exe'), '-version'], env=env, timeout=30)
    assert b'Babacoin' in version
    tests.append('installed daemon runs with package/system PATH only')
    data = root / 'preview-data'
    data.mkdir()
    (data / 'babacoin.conf').write_text('', encoding='ascii')
    settings = root / 'preview-settings'
    (settings / 'Babacoin').mkdir(parents=True)
    (settings / 'Babacoin/Babacoin-Qt-regtest.ini').write_text(
        '[General]\nfAppearanceSetupDone=true\ntheme=Dark\n', encoding='ascii')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    log = (root / 'gui.log').open('wb')
    command = [str(app / 'babacoin-qt.exe'), '-regtest=1', '-datadir=' + str(data),
               '-uiconfigdir=' + str(settings), '-choosedatadir=0', '-server=1',
               '-rpcport=' + str(port), '-rpcbind=127.0.0.1', '-rpcallowip=127.0.0.1',
               '-connect=0', '-listen=0', '-dnsseed=0', '-discover=0', '-upnp=0',
               '-listenonion=0', '-maxconnections=0', '-walletbroadcast=0', '-enablecoinjoin=0']
    process = subprocess.Popen(command, env=env, stdout=log, stderr=log)

    def rpc(method):
        cookie = (data / 'regtest/.cookie').read_bytes().strip()
        request = urllib.request.Request('http://127.0.0.1:%d/' % port,
            data=json.dumps({'jsonrpc': '1.0', 'id': 'installer-test', 'method': method, 'params': []}).encode(),
            headers={'Authorization': 'Basic ' + base64.b64encode(cookie).decode(),
                     'Content-Type': 'application/json'})
        try:
            response = urllib.request.build_opener(urllib.request.ProxyHandler({})).open(request, timeout=10)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            result = json.load(response)
        if result.get('error'):
            raise RuntimeError('GUI RPC not ready')
        return result['result']

    try:
        deadline = time.monotonic() + 180
        while time.monotonic() < deadline:
            assert process.poll() is None, 'Installed GUI exited'
            try:
                chain = rpc('getblockchaininfo')
                assert chain['chain'] == 'regtest' and rpc('getconnectioncount') == 0
                break
            except (OSError, ValueError, RuntimeError):
                time.sleep(0.2)
        else:
            raise RuntimeError('Installed GUI failed to start; see ' + str(root))
        tests.append('installed Qt GUI starts in fresh offline regtest')
        assert install(app, 'reject-running-wallet') != 0
        assert process.poll() is None and rpc('getblockchaininfo')['chain'] == 'regtest'
        verify_payload()
        tests.append('installer refuses running wallet without closing it')
    finally:
        try:
            rpc('stop')
            process.wait(timeout=90)
        finally:
            if process.poll() is None:
                # Only this freshly created synthetic regtest process is owned.
                assert root in data.resolve().parents and '-regtest=1' in command
                process.terminate()
                process.wait(timeout=20)
            log.close()

    # Even user-created wallet files inside the application directory survive.
    (app / 'Data').mkdir()
    (app / 'Data/wallet.dat').write_bytes(sentinel)
    uninstaller = app / 'unins000.exe'
    assert uninstaller.is_file()
    result = subprocess.run([str(uninstaller), '/VERYSILENT', '/SUPPRESSMSGBOXES',
                             '/NORESTART', '/LOG=' + str(root / 'uninstall.log')], timeout=180)
    assert result.returncode == 0
    assert digest(wallet / 'wallet.dat') == wallet_hash
    assert (app / 'Data/wallet.dat').read_bytes() == sentinel
    assert not (app / 'babacoin-qt.exe').exists()
    tests.append('uninstall removes program and preserves external/internal wallet fixtures')
    report = {'result': 'PASS', 'host_architecture': os.environ.get('RUNNER_ARCH', platform.machine()),
              'binary_source_commit': manifest['binary_source_commit'],
              'packaging_source_commit': manifest['packaging_source_commit'],
              'installer_sha256': manifest['installer_sha256'], 'tests': tests,
              'scope': 'Fresh synthetic directories only; complete file hashes, offline GUI, running-app guards and wallet preservation.',
              'fixture_directory': str(root)}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'fixture_directory'}))


if __name__ == '__main__':
    main()
