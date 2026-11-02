#!/usr/bin/env python3
"""Safety/ABI regressions for the Linux installer. No apt, signals or network."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('setup', str(Path(__file__).resolve().parents[2]/'contrib/linux-node-setup.py'))
setup = importlib.util.module_from_spec(spec); spec.loader.exec_module(setup)


class NodeSetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_all_supported_abis(self):
        for os_id, version in setup.PROFILES:
            for machine in ('x86_64', 'aarch64'):
                arch, packages = setup.profile(os_id, version, machine)
                self.assertEqual(arch, machine)
                self.assertIn('libevent-pthreads-2.1-'+setup.PROFILES[(os_id, version)][2], packages)
                self.assertNotIn('libboost-all-dev', packages)
        self.assertIn('libboost-filesystem1.83.0', setup.profile('ubuntu', '24.04', 'x86_64')[1])
        self.assertIn('libboost-chrono1.83.0t64', setup.profile('ubuntu', '24.04', 'aarch64')[1])
        self.assertIn('libboost-filesystem1.74.0', setup.profile('debian', '12', 'x86_64')[1])
        self.assertNotIn('libboost-system1.90.0', setup.profile('ubuntu', '26.04', 'x86_64')[1])

    def test_unsupported_distro_architecture_no_substitution(self):
        for os_id, version, arch in [('ubuntu','16.04','x86_64'),('debian','11','x86_64'),('ubuntu','24.04','armv7l'),('mint','22','x86_64')]:
            with self.assertRaises(RuntimeError): setup.profile(os_id, version, arch)

    def test_os_identification_is_not_executed(self):
        path = self.root/'os-release'
        path.write_text('ID=ubuntu\nVERSION_ID="24.04"\nIGNORED=$(touch bad)\n')
        self.assertEqual(setup.read_os(path), ('ubuntu', '24.04'))
        self.assertFalse((self.root/'bad').exists())

    def test_release_metadata_is_explicit_utf8(self):
        path = self.root/'release.json'
        path.write_bytes('{"body":"\u2014 Babacoin"}'.encode('utf-8'))
        original = Path.read_text
        def enforce_utf8(p, *args, **kwargs):
            self.assertEqual(kwargs.get('encoding'), 'utf-8')
            return original(p, *args, **kwargs)
        with patch.object(setup, 'download', return_value=path), patch.object(Path, 'read_text', enforce_utf8):
            self.assertEqual(setup.get_json('https://example.invalid', self.root, 'ignored')['body'], '\u2014 Babacoin')

    def test_asset_origin_stable_tag_and_exact_path(self):
        self.assertEqual(setup.asset_url('v2.0.2', 'x'), 'https://github.com/babacoinbbc/babacoin/releases/download/v2.0.2/x')
        for tag, name in [('v2.0.2-rc.1','x'),('main','x'),('v2.0.2','../wallet.dat'),('v2.0.2','https://attacker.invalid/x')]:
            with self.assertRaises(RuntimeError): setup.asset_url(tag, name)

    def test_wrong_checksum_rejected(self):
        release = {'tag_name':'v2.0.2'}
        with patch.object(setup, 'download') as download:
            with self.assertRaises(RuntimeError): setup.fetch_asset(release, 'x', self.root, 'not-a-sha256')
            download.assert_not_called()

    def test_multiple_nodes_require_explicit_selection(self):
        a = {'data': self.root/'a'}; b = {'data': self.root/'b'}
        with self.assertRaises(RuntimeError): setup.select_node([a,b], None)
        self.assertIs(setup.select_node([a,b], a['data']), a)
        with self.assertRaises(RuntimeError): setup.select_node([a,b], self.root/'c')

    def test_pid_reuse_never_signals(self):
        with patch.object(setup, 'identity', return_value='new'), patch.object(setup.os, 'kill') as kill:
            with self.assertRaises(RuntimeError): setup.stop_node({'pid':123,'token':'old','unit':None})
            kill.assert_not_called()

    def test_shutdown_timeout_never_force_kills(self):
        node = {'pid':123,'token':'same','unit':None}
        with patch.object(setup, 'identity', return_value='same'), patch.object(setup.os, 'kill') as kill, patch.object(setup.time, 'monotonic', side_effect=[0,2]):
            with self.assertRaises(RuntimeError): setup.stop_node(node, timeout=1)
            kill.assert_called_once_with(123, setup.signal.SIGTERM)

    def test_systemd_requires_matching_mainpid(self):
        with patch.object(setup, 'identity', return_value='same'), patch.object(setup.subprocess, 'check_output', return_value='999'), patch.object(setup.subprocess, 'check_call') as call:
            with self.assertRaises(RuntimeError): setup.stop_node({'pid':123,'token':'same','unit':'bbc.service'})
            call.assert_not_called()

    def test_nonmainnet_custom_paths_rejected(self):
        data = self.root/'data'; data.mkdir()
        for args in (['-regtest=1'], ['-testnet'], ['-walletdir=/external']):
            with self.assertRaises(RuntimeError): setup.require_mainnet(data, args)
        conf = data/'babacoin.conf'
        for contents in ('regtest=1\n', '[main]\nwalletdir=/external\n', 'datadir=/elsewhere\n', 'includeconf=another.conf\n'):
            conf.write_text(contents)
            with self.assertRaises(RuntimeError): setup.require_mainnet(data, [])

    def test_private_wallet_backup_is_verified_and_unchanged(self):
        data = self.root/'data'; (data/'wallets/named').mkdir(parents=True)
        (data/'wallet.dat').write_bytes(b'PRIVATE-TEST-WALLET')
        (data/'wallets/named/wallet.dat').write_bytes(b'OTHER-TEST-WALLET')
        (data/'babacoin.conf').write_text('rpcpassword=synthetic-test-value\n')
        (data/'bootstrap.dat').write_bytes(b'PUBLIC-NOT-A-WALLET')
        dest = self.root/'backup'; dest.mkdir()
        records = setup.backup_wallet(data, dest)
        self.assertEqual(set(records), {'wallet.dat','wallets/named/wallet.dat','babacoin.conf'})
        for name, digest in records.items():
            self.assertEqual(setup.digest(data/name), digest)
            self.assertEqual(setup.digest(dest/name), digest)
        self.assertFalse((dest/'bootstrap.dat').exists())

    def zip_bootstrap(self, extra=None, duplicate=False):
        raw = bytes.fromhex('035dd72f')+b'\x00'*84
        path = self.root/'bootstrap.zip'
        with zipfile.ZipFile(str(path), 'w') as bundle:
            bundle.writestr('bootstrap.dat', raw)
            if extra: bundle.writestr(extra, b'BAD')
            if duplicate: bundle.writestr('bootstrap.dat', raw)
        return path, dict(first_height=0, network_magic='035dd72f', genesis_hash=setup.GENESIS,
                          payload_bytes=len(raw), payload_sha256=hashlib.sha256(raw).hexdigest())

    def test_bootstrap_only_full_mainnet_and_exact_hash(self):
        path, record = self.zip_bootstrap()
        target = self.root/'bootstrap.dat'
        setup.unpack_bootstrap(path, record, target)
        self.assertEqual(setup.digest(target), record['payload_sha256'])
        with self.assertRaises(FileExistsError): setup.unpack_bootstrap(path, record, target)
        for key, bad in [('first_height',5),('genesis_hash','0'*64),('payload_bytes',1),('payload_sha256','f'*64)]:
            other = dict(record); other[key] = bad
            with self.assertRaises(RuntimeError): setup.unpack_bootstrap(path, other, self.root/('bad-'+key))

    def test_bootstrap_archive_cannot_write_wallets_or_escape(self):
        for name in ('wallet.dat', '../wallet.dat', '/etc/passwd'):
            path, record = self.zip_bootstrap(name)
            with self.assertRaises(RuntimeError): setup.unpack_bootstrap(path, record, self.root/'output')
            self.assertFalse((self.root/'output').exists())


if __name__ == '__main__': unittest.main()
