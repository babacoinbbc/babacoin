#!/usr/bin/env python3
"""Reject hostile packages before any destination or wallet can be modified."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('verify_release_archive',
    str(Path(__file__).resolve().parents[2] / 'contrib/verify-release-archive.py'))
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class ArchiveSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bbc-archive-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / 'package.tar.gz'
        self.destination = self.root / 'unpacked'
        self.wallet = self.root / 'wallet.dat'
        self.wallet.write_bytes(b'EXISTING-WALLET-DO-NOT-TOUCH')

    def write_package(self, extra=None, machine=62, missing=False):
        header = b'\x7fELF\x02\x01' + b'\0' * 12 + machine.to_bytes(2, 'little')
        with tarfile.open(str(self.archive), 'w:gz') as bundle:
            for name in sorted(verifier.PROGRAMS - ({'babacoind'} if missing else set())):
                member = tarfile.TarInfo('./' + name)
                member.size = len(header)
                bundle.addfile(member, io.BytesIO(header))
            if extra:
                bundle.addfile(extra, io.BytesIO(b'BAD') if extra.isfile() else None)

    def reject(self):
        with self.assertRaises(ValueError):
            verifier.extract(self.archive, self.destination, 'x86_64')
        self.assertFalse(self.destination.exists())
        self.assertEqual(self.wallet.read_bytes(), b'EXISTING-WALLET-DO-NOT-TOUCH')

    def test_path_traversal_does_not_touch_wallet(self):
        extra = tarfile.TarInfo('../wallet.dat'); extra.size = 3
        self.write_package(extra); self.reject()

    def test_symlink_is_rejected(self):
        extra = tarfile.TarInfo('SOURCE_COMMIT')
        extra.type = tarfile.SYMTYPE; extra.linkname = '../wallet.dat'
        self.write_package(extra); self.reject()

    def test_duplicate_program_is_rejected(self):
        extra = tarfile.TarInfo('babacoind'); extra.size = 3
        self.write_package(extra); self.reject()

    def test_wrong_architecture_is_rejected(self):
        self.write_package(machine=183); self.reject()

    def test_missing_program_is_rejected(self):
        self.write_package(missing=True); self.reject()

    def test_existing_destination_is_not_overwritten(self):
        self.write_package(); self.destination.mkdir()
        sentinel = self.destination / 'babacoind'; sentinel.write_bytes(b'WORKING-BINARY')
        with self.assertRaises(ValueError):
            verifier.extract(self.archive, self.destination, 'x86_64')
        self.assertEqual(sentinel.read_bytes(), b'WORKING-BINARY')

    def test_valid_package_is_extracted_privately(self):
        self.write_package()
        verifier.extract(self.archive, self.destination, 'x86_64')
        self.assertEqual({p.name for p in self.destination.iterdir()}, verifier.PROGRAMS)
        self.assertEqual(self.wallet.read_bytes(), b'EXISTING-WALLET-DO-NOT-TOUCH')


if __name__ == '__main__':
    unittest.main()
