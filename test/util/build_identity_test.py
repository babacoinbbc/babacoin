#!/usr/bin/env python3
"""Real Git/export and no-Git build identity regressions; temporary files only."""
import io
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SHELL = os.environ.get('BBC_TEST_SH') or shutil.which('sh')


@unittest.skipUnless(SHELL and shutil.which('git'), 'POSIX shell and Git required')
class BuildIdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'share').mkdir()
        shutil.copyfile(str(ROOT/'share/genbuild.sh'), str(self.root/'share/genbuild.sh'))
        self.env = {k: v for k, v in os.environ.items() if not k.startswith('GIT_')}
        self.env.pop('BBC_SOURCE_COMMIT', None)
        self.env.pop('BITCOIN_GENBUILD_NO_GIT', None)
        self.env['GIT_CONFIG_NOSYSTEM'] = '1'
        self.env['GIT_CONFIG_GLOBAL'] = os.devnull
        self.env['PATH'] = str(Path(SHELL).parent) + os.pathsep + self.env.get('PATH', '')

    def git(self, *args):
        return subprocess.check_output(['git', '-c', 'core.autocrlf=false',
                                        '-c', 'commit.gpgsign=false',
                                        '-c', 'user.name=Build Identity QA',
                                        '-c', 'user.email=qa@example.invalid'] + list(args),
                                       cwd=str(self.root), env=self.env)

    def commit_fixture(self):
        self.git('init', '-q')
        self.git('add', '.')
        self.git('commit', '-qm', 'Synthetic build identity fixture')
        return self.git('rev-parse', 'HEAD').decode().strip()

    def generate(self, commit=None):
        env = dict(self.env)
        if commit is not None:
            env['BBC_SOURCE_COMMIT'] = commit
        return subprocess.run([SHELL, (self.root/'share/genbuild.sh').as_posix(),
                               (self.root/'build.h').as_posix(), self.root.as_posix()],
                              cwd=str(self.root), env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_no_git_uses_full_explicit_source_commit(self):
        result = self.generate('a'*40)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((self.root/'build.h').read_text().strip(), '#define BUILD_SUFFIX aaaaaaa')

    def test_invalid_explicit_source_is_rejected_without_rewriting_header(self):
        target = self.root/'build.h'
        target.write_text('UNCHANGED\n')
        for commit in ('abc', 'A'*40, 'a'*39+';', 'a'*40+'\n'):
            with self.subTest(commit=commit):
                self.assertNotEqual(self.generate(commit).returncode, 0)
                self.assertEqual(target.read_text(), 'UNCHANGED\n')

    def test_git_mismatch_is_rejected(self):
        commit = self.commit_fixture()
        result = self.generate('0'*40 if commit != '0'*40 else '1'*40)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root/'build.h').exists())

    def test_git_identity_keeps_dirty_status(self):
        (self.root/'tracked.txt').write_text('original\n')
        commit = self.commit_fixture()
        result = self.generate(commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(commit[:7], (self.root/'build.h').read_text())
        (self.root/'tracked.txt').write_text('changed\n')
        result = self.generate(commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(commit[:7]+'-dirty', (self.root/'build.h').read_text())

    def test_git_archive_embeds_its_actual_commit(self):
        (self.root/'src').mkdir()
        shutil.copyfile(str(ROOT/'src/clientversion.cpp'), str(self.root/'src/clientversion.cpp'))
        (self.root/'.gitattributes').write_text('src/clientversion.cpp export-subst\n')
        commit = self.commit_fixture()
        with tarfile.open(fileobj=io.BytesIO(self.git('archive', 'HEAD'))) as archive:
            source = archive.extractfile('src/clientversion.cpp').read().decode()
        self.assertIn('#define GIT_ARCHIVE 1', source)
        self.assertIn('#define GIT_COMMIT_ID "'+commit+'"', source)
        self.assertNotIn('4ddcd254cceebdb63c7b205a93b4719e46c2d2a2', source)


if __name__ == '__main__':
    unittest.main()
