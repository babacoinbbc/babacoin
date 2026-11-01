#!/usr/bin/env python3
"""Assemble only the complete, checksum-verified, same-source V2 matrix."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import tarfile
import zipfile
from release_policy import check_source


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--source-commit', required=True)
    args = parser.parse_args()
    check_source(args.tag)
    if not re.fullmatch(r'v[0-9][A-Za-z0-9._-]*', args.tag):
        raise ValueError('Invalid release tag')
    if not re.fullmatch(r'[0-9a-f]{40}', args.source_commit):
        raise ValueError('A full source commit is required')
    expected = []
    for distro, versions in [('ubuntu', ['18.04', '20.04', '22.04', '24.04', '26.04']),
                             ('debian', ['12', '13'])]:
        for version in versions:
            for arch, machine in [('x86_64', 62), ('aarch64', 183)]:
                artifact_arch = 'arm64' if arch == 'aarch64' else arch
                expected.append(('babacoin-tested-%s-%s-%s' % (distro, version, artifact_arch),
                                 'babacoin-%s-%s-%s.tar.gz' % (distro, version, arch), 'elf', machine))
    for arch, machine in [('x86_64', 0x1000007), ('arm64', 0x100000c)]:
        expected.append(('babacoin-tested-macos-' + arch,
                         'babacoin-macos-%s-test.tar.gz' % arch, 'macho', machine))
    expected.append(('babacoin-tested-windows-x64', 'babacoin-windows-x86_64-test.zip', 'pe', 0x8664))
    # The ARM64 host runs the x64 Windows package under emulation; it is not a
    # second native-ARM64 application or a reason to relabel the x64 executable.
    arm = json.loads((args.artifacts / 'babacoin-tested-windows-arm64-emulation/native-windows-tests.json').read_text())
    if (arm['result'] != 'PASS' or arm['source_commit'] != args.source_commit
            or arm['host_runner_architecture'].upper() != 'ARM64'):
        raise ValueError('Windows on ARM64 emulation tests did not pass for this source')
    args.output.mkdir(exist_ok=False)
    manifest = {'tag': args.tag, 'source_commit': args.source_commit,
                'windows_arm64': 'x86_64 binaries tested under ARM64-host emulation', 'packages': []}
    for folder, name, kind, machine in expected:
        root = args.artifacts / folder
        archive = root / name
        checksum = (root / (name + '.sha256')).read_text(encoding='utf-8-sig').split()[0].lower()
        if not re.fullmatch(r'[a-f0-9]{64}', checksum) or digest(archive) != checksum:
            raise ValueError('Archive checksum mismatch: ' + name)
        binaries = {}
        if kind == 'pe':
            report = json.loads((root / 'native-windows-tests.json').read_text())
            if report['result'] != 'PASS' or report['source_commit'] != args.source_commit:
                raise ValueError('Windows test report/source mismatch')
            with zipfile.ZipFile(archive) as bundle:
                source = bundle.read('SOURCE_COMMIT').decode('utf-8-sig').strip()
                for program in ['babacoind', 'babacoin-cli', 'babacoin-tx', 'babacoin-qt']:
                    content = bundle.read(program + '.exe')
                    offset = int.from_bytes(content[60:64], 'little')
                    if content[offset:offset + 4] != b'PE\0\0' or int.from_bytes(content[offset + 4:offset + 6], 'little') != machine:
                        raise ValueError('Wrong PE architecture')
                    key = ('qt/' if program == 'babacoin-qt' else '') + program + '.exe'
                    value = hashlib.sha256(content).hexdigest()
                    if report['binaries_sha256'][key] != value:
                        raise ValueError('Windows executable changed after testing')
                    binaries[program] = value
                if 'TEST-WALLET.cmd' not in bundle.namelist():
                    raise ValueError('Missing isolated Windows launcher')
        else:
            source = (root / 'SOURCE_COMMIT').read_text().strip()
            with tarfile.open(archive) as bundle:
                members = {str(Path(m.name)): m for m in bundle.getmembers() if m.isfile()}
                for program in ['babacoind', 'babacoin-cli', 'babacoin-tx', 'babacoin-qt']:
                    member = members[('bin/' if kind == 'macho' else '') + program]
                    content = bundle.extractfile(member).read()
                    if kind == 'elf':
                        valid = content[:6] == b'\x7fELF\x02\x01' and int.from_bytes(content[18:20], 'little') == machine
                    else:
                        valid = content[:4] == b'\xcf\xfa\xed\xfe' and int.from_bytes(content[4:8], 'little') == machine
                    if not valid:
                        raise ValueError('Wrong executable architecture: ' + name)
                    binaries[program] = hashlib.sha256(content).hexdigest()
                if 'TEST-WALLET.sh' not in members:
                    raise ValueError('Missing isolated GUI launcher')
        if source != args.source_commit:
            raise ValueError('Mixed source revisions in release: ' + name)
        # Input artifacts carry a test suffix; stable end-user archives do not.
        package_name = name[len('babacoin-'):].replace('-test.', '.')
        published = 'babacoin-' + args.tag + '-' + package_name
        shutil.copyfile(archive, args.output / published)
        (args.output / (published + '.sha256')).write_text(checksum + '  ' + published + '\n', encoding='ascii')
        manifest['packages'].append({'name': published, 'sha256': checksum, 'format': kind,
                                     'machine': machine, 'binaries_sha256': binaries})
    (args.output / 'SHA256SUMS').write_text(''.join(p['sha256'] + '  ' + p['name'] + '\n' for p in manifest['packages']), encoding='ascii')
    (args.output / 'RELEASE-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Verified %d same-source platform packages for %s.' % (len(expected), args.tag))


if __name__ == '__main__':
    main()
