#!/usr/bin/env python3
"""Prevent RC binaries from being packaged as the final stable release."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'ci'))
from release_policy import require_release_identity, check_source


class ReleaseStatusTests(unittest.TestCase):
    def test_stable_and_candidate_identity(self):
        require_release_identity('v2.0.0', 'define(_CLIENT_VERSION_IS_RELEASE, true)\n')
        require_release_identity('v2.0.0-mainnet', 'define(_CLIENT_VERSION_IS_RELEASE, true)\n')
        require_release_identity('v2.0.0-rc.4', 'define(_CLIENT_VERSION_IS_RELEASE, false)\n')

    def test_metadata_cannot_promote_rc_binaries(self):
        with self.assertRaises(ValueError):
            require_release_identity('v2.0.0', 'define(_CLIENT_VERSION_IS_RELEASE, false)\n')
        with self.assertRaises(ValueError):
            require_release_identity('v2.0.0-mainnet', 'define(_CLIENT_VERSION_IS_RELEASE, false)\n')
        with self.assertRaises(ValueError):
            require_release_identity('v2.0.0-rc.4', 'define(_CLIENT_VERSION_IS_RELEASE, true)\n')

    def test_unknown_or_ambiguous_identity(self):
        for tag, config in [('v2.0.0-rc.0', 'define(_CLIENT_VERSION_IS_RELEASE, false)\n'),
                            ('v2.0.0', ''), ('v2.0.0', 'define(_CLIENT_VERSION_IS_RELEASE, true)\ndefine(_CLIENT_VERSION_IS_RELEASE, false)\n')]:
            with self.assertRaises(ValueError):
                require_release_identity(tag, config)

    def test_current_source_is_final_v2(self):
        check_source('v2.0.0')
        check_source('v2.0.0-mainnet')


if __name__ == '__main__':
    unittest.main()
