#!/usr/bin/env python3
"""Reject stable/RC and compiled-version mismatches before publication."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'ci'))
from release_policy import require_release_identity, check_source


def config(flag='true', version=(2, 0, 1, 0)):
    return ''.join('define(_CLIENT_VERSION_%s, %d)\n' % (name, value)
                   for name, value in zip(('MAJOR', 'MINOR', 'REVISION', 'BUILD'), version)) + \
        'define(_CLIENT_VERSION_IS_RELEASE, %s)\n' % flag


class ReleaseStatusTests(unittest.TestCase):
    def test_stable_and_candidate_identity(self):
        require_release_identity('v2.0.1', config())
        require_release_identity('v2.0.1-mainnet', config())
        require_release_identity('v2.0.1-rc.4', config('false'))

    def test_metadata_cannot_promote_rc_binaries(self):
        for tag, source in [('v2.0.1', config('false')), ('v2.0.1-mainnet', config('false')),
                            ('v2.0.1-rc.4', config())]:
            with self.assertRaises(ValueError):
                require_release_identity(tag, source)

    def test_tag_must_match_compiled_version(self):
        with self.assertRaises(ValueError):
            require_release_identity('v2.0.1', config(version=(2, 0, 0, 0)))
        require_release_identity('v2.0.1.7', config(version=(2, 0, 1, 7)))

    def test_unknown_or_ambiguous_identity(self):
        for tag, source in [('v2.0.1-rc.0', config('false')), ('v2.0.1', ''),
                            ('v2.0.1', config() + 'define(_CLIENT_VERSION_IS_RELEASE, false)\n'),
                            ('v2.0.1', config() + 'define(_CLIENT_VERSION_REVISION, 2)\n')]:
            with self.assertRaises(ValueError):
                require_release_identity(tag, source)

    def test_current_source_is_final_v2(self):
        check_source('v2.0.2')
        check_source('v2.0.2-mainnet')


if __name__ == '__main__':
    unittest.main()
