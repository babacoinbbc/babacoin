#!/usr/bin/env python3
"""Extract a checked Linux package into a NEW private directory, never a wallet."""
import argparse
from pathlib import Path, PurePosixPath
import shutil
import tarfile

PROGRAMS = {'babacoind', 'babacoin-cli', 'babacoin-tx', 'babacoin-qt'}
ALLOWED = PROGRAMS | {'TEST-WALLET.sh', 'TEST-WALLET-README.txt', 'SOURCE_COMMIT',
                      'RUNTIME-LIBRARIES.txt', 'PACKAGES.tsv', 'COPYING', 'DEPENDENCIES.md'}


def extract(archive, destination, architecture):
    machine = {'x86_64': 62, 'aarch64': 183}[architecture]
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise ValueError('The extraction directory must not already exist')
    seen = set()
    total = 0
    # Validate every member before creating or writing anything.
    with tarfile.open(str(archive), 'r:gz') as bundle:
        members = []
        for member in bundle.getmembers():
            path = PurePosixPath(member.name)
            if member.isdir() and str(path) == '.':
                continue
            if (not member.isfile() or path.is_absolute() or len(path.parts) != 1
                    or str(path) not in ALLOWED or str(path) in seen):
                raise ValueError('Unexpected, duplicate or unsafe archive member')
            if not 0 <= member.size <= 512 * 1024 * 1024:
                raise ValueError('Archive member exceeds its size limit')
            total += member.size
            if total > 2 * 1024 * 1024 * 1024:
                raise ValueError('Archive exceeds its size limit')
            seen.add(str(path))
            if str(path) in PROGRAMS:
                with bundle.extractfile(member) as stream:
                    header = stream.read(20)
                if (header[:6] != b'\x7fELF\x02\x01' or len(header) != 20
                        or int.from_bytes(header[18:20], 'little') != machine):
                    raise ValueError('Executable architecture does not match this host')
            members.append((member, str(path)))
        if not PROGRAMS.issubset(seen):
            raise ValueError('Package is missing required applications')
        destination.mkdir(mode=0o700)
        for member, name in members:
            target = destination / name
            with bundle.extractfile(member) as source, target.open('xb') as output:
                shutil.copyfileobj(source, output)
            target.chmod(0o700 if name in PROGRAMS or name.endswith('.sh') else 0o600)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('architecture', choices=['x86_64', 'aarch64'])
    args = parser.parse_args()
    extract(args.archive, args.destination, args.architecture)
