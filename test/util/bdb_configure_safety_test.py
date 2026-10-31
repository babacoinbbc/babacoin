#!/usr/bin/env python3
"""Exercise the real BDB configure macro with isolated synthetic headers.

These tests check option enforcement and link failure handling; they are not
database format/restore tests. Those require actual old/new daemon binaries.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class BDBConfigureSafetyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="bbc-bdb-configure-")
        cls.root = Path(cls.temp.name)
        repo = Path(__file__).resolve().parents[2]
        macro = repo / "build-aux/m4/bitcoin_find_bdb48.m4"
        (cls.root / "bdb.m4").write_text(macro.read_text(encoding="utf-8"), encoding="utf-8")
        (cls.root / "configure.ac").write_text('''AC_INIT([bdb-portability-test], [1])
AC_PROG_CXX
AC_LANG([C++])
AC_DEFUN([BITCOIN_SUBDIR_TO_INCLUDE], [BDB_CPPFLAGS="$CPPFLAGS"])
m4_include([bdb.m4])
BITCOIN_FIND_BDB48
AC_OUTPUT
''', encoding="utf-8")
        subprocess.run(["autoconf"], cwd=str(cls.root), check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def configure(self, major, minor, option=None, library="-lm", library_version=None):
        with tempfile.TemporaryDirectory(dir=str(self.root)) as directory:
            path = Path(directory)
            headers = path / "headers"
            headers.mkdir()
            actual_major, actual_minor = library_version or (major, minor)
            (headers / "db_cxx.h").write_text('''#define DB_VERSION_MAJOR %d
#define DB_VERSION_MINOR %d
class Db { public: Db(void*, unsigned) {} int close(unsigned) { return 0; } };
inline const char* db_version(int* major, int* minor, int*) {
    *major = %d; *minor = %d; return "synthetic library";
}
''' % (major, minor, actual_major, actual_minor), encoding="utf-8")
            command = ["sh", str(self.root / "configure"),
                       "BDB_CFLAGS=-I" + str(headers), "BDB_LIBS=" + library]
            if option:
                command.append(option)
            env = dict(os.environ)
            env.pop("CONFIG_SITE", None)
            return subprocess.run(command, cwd=str(path), env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  universal_newlines=True, timeout=120)

    def test_portable_headers_pass(self):
        result = self.configure(4, 8)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_manual_flags_do_not_bypass_portability(self):
        result = self.configure(5, 3)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("4.8 is required for portable wallets", result.stdout)

    def test_explicit_no_is_not_consent(self):
        result = self.configure(5, 3, "--without-incompatible-bdb")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("4.8 is required for portable wallets", result.stdout)

    def test_explicit_yes_warns_and_passes(self):
        result = self.configure(5, 3, "--with-incompatible-bdb")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("may not be usable by older releases", result.stdout)

    def test_invalid_option_is_rejected(self):
        result = self.configure(5, 3, "--with-incompatible-bdb=maybe")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("accepts only yes or no", result.stdout)

    def test_unlinkable_library_is_rejected(self):
        result = self.configure(4, 8, library="-lbbc_nonexistent_library")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not link", result.stdout)

    def test_linkable_mismatched_version_is_rejected(self):
        result = self.configure(4, 8, library_version=(5, 3))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("library version does not match its headers", result.stdout)


if __name__ == "__main__":
    unittest.main()
