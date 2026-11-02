#!/usr/bin/env python3
"""Bootstrap framing, integrity, chain selection and publication boundaries."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('bootstrap_export', ROOT / 'contrib/bootstrap/export-bootstrap.py')
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


def transaction(version=1, payload=b''):
    # Synthetic framing fixture; this is not a spendable/consensus-valid tx.
    tx = struct.pack('<I', version) + b'\x00\x00' + bytes(4)
    return tx + (bytes([len(payload)]) + payload if version & 0xffff == 3 and version >> 16 else b'')


def block(previous=bytes(32), txs=None, nonce=0):
    txs = txs or [transaction()]
    hashes = [exporter.sha256d(tx) for tx in txs]
    while len(hashes) > 1:
        if len(hashes) & 1:
            hashes.append(hashes[-1])
        hashes = [exporter.sha256d(hashes[i] + hashes[i + 1]) for i in range(0, len(hashes), 2)]
    return struct.pack('<I', 4) + previous + hashes[0] + struct.pack('<III', 1, 0x20001fff, nonce) + bytes([len(txs)]) + b''.join(txs)


class BootstrapTests(unittest.TestCase):
    def test_mainnet_genesis_block_id_uses_sha256d_not_pow(self):
        header = bytes.fromhex('0400000000000000000000000000000000000000000000000000000000000000000000006e7a209c737c74026555905b2285607af3859277545fba58180c9cd18155db4135f42163ff1f0020ad020000')
        self.assertEqual(exporter.sha256d(header)[::-1].hex(), exporter.GENESIS)
        legacy_spec = importlib.util.spec_from_file_location('linearize', ROOT / 'contrib/linearize/linearize-data.py')
        legacy = importlib.util.module_from_spec(legacy_spec)
        legacy_spec.loader.exec_module(legacy)
        self.assertEqual(legacy.calc_hdr_hash(header), exporter.sha256d(header))

    def test_normal_and_v3_special_transactions(self):
        for tx in [transaction(), transaction(0x10003, b'BBC'), transaction(0x10004)]:
            data = block(txs=[tx])
            self.assertEqual(exporter.validate_block(data), exporter.sha256d(data[:80]))

    def test_corruption_truncation_and_trailing_bytes(self):
        data = block()
        for invalid in [data[:-1], data + b'\0', data[:40] + bytes([data[40] ^ 1]) + data[41:], data[:80] + b'\xfd\x01\x00' + data[81:]]:
            with self.assertRaises(ValueError):
                exporter.validate_block(invalid)

    def test_merkle_mutation_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Mutated'):
            exporter.validate_block(block(txs=[transaction(), transaction()]))

    def test_missing_ancestry_and_incorrect_height(self):
        with self.assertRaises(ValueError):
            exporter.canonical_chain({}, '00' * 31 + '01', 0)
        genesis = bytes.fromhex(exporter.GENESIS)[::-1]
        with self.assertRaises(ValueError):
            exporter.canonical_chain({genesis: (bytes(32), 0, 0, 81)}, exporter.GENESIS, 1)

    def test_export_selects_pinned_fork_and_excludes_private_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, output = root / 'blocks', root / 'public'
            source.mkdir()
            a = block()
            b = block(exporter.sha256d(a[:80]), nonce=1)
            fork = block(exporter.sha256d(a[:80]), nonce=2)
            (source / 'blk00000.dat').write_bytes(b''.join(exporter.MAGIC + struct.pack('<I', len(p)) + p for p in [a, fork, b]) + bytes(10))
            (source / 'wallet.dat').write_bytes(b'PRIVATE-WALLET-SENTINEL')
            (source / 'babacoin.conf').write_bytes(b'PRIVATE-CREDENTIAL-SENTINEL')
            original = exporter.GENESIS
            try:
                exporter.GENESIS = exporter.sha256d(a[:80])[::-1].hex()
                with contextlib.redirect_stdout(io.StringIO()):
                    exporter.export(source, output, 1, exporter.sha256d(b[:80])[::-1].hex(), 1)
            finally:
                exporter.GENESIS = original
            manifest = json.loads((output / 'BOOTSTRAP-MANIFEST.json').read_text())
            self.assertEqual(manifest['archives'][1]['required_base_hash'], exporter.sha256d(a[:80])[::-1].hex())
            for record in manifest['archives']:
                with zipfile.ZipFile(output / record['archive_name']) as z:
                    self.assertEqual(set(z.namelist()), {record['payload_name'], 'CONTENTS.json', 'BOOTSTRAP-README.md'})
                    raw = z.read(record['payload_name'])
                    self.assertNotIn(b'PRIVATE-', raw)
                    self.assertNotIn(fork, raw)
                    self.assertEqual(len(raw), record['payload_bytes'])
            with self.assertRaises(FileExistsError):
                exporter.export(source, output, 1, '00' * 32, 1)
            with self.assertRaises(ValueError):
                exporter.export(source, source / 'out', 1, '00' * 32, 1)


if __name__ == '__main__':
    unittest.main()
