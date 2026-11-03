#!/usr/bin/env python3
"""Mach-O dependency and library-origin regressions; no native execution."""
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('deploy', str(Path(__file__).resolve().parents[2]/'ci/deploy-macos-libraries.py'))
deploy = importlib.util.module_from_spec(spec); spec.loader.exec_module(deploy)
ARM = 0x100000c
INTEL = 0x1000007


def image(cpu=ARM, names=()):
    commands = b''
    for command, name in names:
        value = name.encode()+b'\0'
        size = (24+len(value)+7)//8*8
        commands += struct.pack('<IIIIII', command, size, 24, 0, 0, 0)+value+b'\0'*(size-24-len(value))
    return struct.pack('<IIIIIIII', 0xfeedfacf, cpu, 0, 6, len(names), len(commands), 0, 0)+commands


class MacLibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.prefix = self.root/'brew'; self.prefix.mkdir()

    def library(self, relative, cpu=ARM):
        path = self.prefix/relative; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(image(cpu)); return path

    def test_install_id_is_not_a_runtime_dependency(self):
        path = self.root/'plugin.dylib'
        path.write_bytes(image(names=[(0xd, '/build/plugin.dylib'), (0xc, '@loader_path/libboost_atomic.dylib'),
                                      (0x80000018, '/usr/lib/libSystem.B.dylib')]))
        cpu, loads = deploy.macho(path)
        self.assertEqual(cpu, ARM)
        self.assertEqual(loads, ['@loader_path/libboost_atomic.dylib', '/usr/lib/libSystem.B.dylib'])

    def test_missing_boost_sibling_is_resolved_from_native_prefix(self):
        actual = self.library('lib/libboost_atomic.dylib')
        self.assertEqual(deploy.homebrew_library(self.prefix, actual.name, ARM), actual.resolve())

    def test_mismatched_architecture_is_rejected(self):
        self.library('lib/libboost_atomic.dylib', INTEL)
        with self.assertRaises(RuntimeError): deploy.homebrew_library(self.prefix, 'libboost_atomic.dylib', ARM)

    def test_ambiguous_formula_versions_are_rejected(self):
        self.library('opt/a/lib/libtest.dylib'); self.library('opt/b/lib/libtest.dylib')
        with self.assertRaises(RuntimeError): deploy.homebrew_library(self.prefix, 'libtest.dylib', ARM)

    @unittest.skipIf(os.name == 'nt', 'POSIX symlink support required')
    def test_external_symlink_cannot_supply_a_library(self):
        external = self.root/'outside.dylib'; external.write_bytes(image())
        (self.prefix/'lib').mkdir()
        (self.prefix/'lib/libtest.dylib').symlink_to(external)
        with self.assertRaises(RuntimeError): deploy.homebrew_library(self.prefix, 'libtest.dylib', ARM)

    def test_truncated_load_command_is_rejected(self):
        path = self.root/'bad.dylib'
        path.write_bytes(image(names=[(0xc, '@loader_path/a.dylib')])[:-8])
        with self.assertRaises(ValueError): deploy.macho(path)


if __name__ == '__main__': unittest.main()
