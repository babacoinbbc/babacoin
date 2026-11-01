"""Reject publication metadata that contradicts the compiled release flag."""
from pathlib import Path
import re


def require_release_identity(tag, config):
    stable = tag in ('v2.0.0', 'v2.0.0-mainnet')
    candidate = re.fullmatch(r'v2\.0\.0-rc\.[1-9][0-9]*', tag) is not None
    if not stable and not candidate:
        raise ValueError('Only V2.0.0 or an explicit V2.0.0 release candidate is supported')
    matches = re.findall(r'^define\(_CLIENT_VERSION_IS_RELEASE, (true|false)\)\s*$', config, re.MULTILINE)
    if len(matches) != 1 or (matches[0] == 'true') != stable:
        raise ValueError('Release tag contradicts CLIENT_VERSION_IS_RELEASE; rebuild from the correct source')


def check_source(tag):
    require_release_identity(tag, (Path(__file__).resolve().parents[1] / 'configure.ac').read_text())
