"""Reject publication metadata that contradicts the compiled release flag."""
from pathlib import Path
import re


def require_release_identity(tag, config):
    parts = []
    for name in ('MAJOR', 'MINOR', 'REVISION', 'BUILD'):
        values = re.findall(r'^define\(_CLIENT_VERSION_' + name + r',\s*([0-9]+)\)\s*$', config, re.MULTILINE)
        if len(values) != 1:
            raise ValueError('Missing or ambiguous compiled version component: ' + name)
        parts.append(int(values[0]))
    if parts[0] != 2:
        raise ValueError('This release pipeline supports the V2 series')
    version = '.'.join(str(n) for n in (parts if parts[3] else parts[:3]))
    stable = tag in ('v' + version, 'v' + version + '-mainnet')
    candidate = re.fullmatch(re.escape('v' + version) + r'-rc\.[1-9][0-9]*', tag) is not None
    if not stable and not candidate:
        raise ValueError('Release tag does not match compiled version ' + version)
    matches = re.findall(r'^define\(_CLIENT_VERSION_IS_RELEASE, (true|false)\)\s*$', config, re.MULTILINE)
    if len(matches) != 1 or (matches[0] == 'true') != stable:
        raise ValueError('Release tag contradicts CLIENT_VERSION_IS_RELEASE; rebuild from the correct source')


def check_source(tag):
    require_release_identity(tag, (Path(__file__).resolve().parents[1] / 'configure.ac').read_text())


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    check_source(parser.parse_args().tag)
