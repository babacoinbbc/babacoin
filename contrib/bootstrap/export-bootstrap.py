#!/usr/bin/env python3
"""Export portable BBC mainnet blocks from a stopped node's cold block files.

Only blkNNNNN.dat files are read. No wallet, configuration, chainstate, peer or
credential files are exported. Consumers must still perform normal consensus
validation; structural and Merkle checks here are not a consensus replay.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct
import zipfile

MAGIC = bytes.fromhex('035dd72f')
GENESIS = '84de4877419c696744198422de2628087ae2270c73fb370ab4cfe2fe01061854'
MAX_BLOCK = 32 * 1024 * 1024


def sha256d(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, count):
        if count < 0 or count > len(self.data) - self.pos:
            raise ValueError('Truncated block or transaction')
        start = self.pos
        self.pos += count
        return self.data[start:self.pos]

    def compact(self):
        first = self.take(1)[0]
        if first < 253:
            return first
        width, minimum = {253: (2, 253), 254: (4, 65536), 255: (8, 4294967296)}[first]
        value = int.from_bytes(self.take(width), 'little')
        if value < minimum or value > MAX_BLOCK:
            raise ValueError('Noncanonical or excessive CompactSize')
        return value


def validate_block(data):
    """Check BBC non-witness transaction framing and the full Merkle root."""
    reader = Reader(data)
    header = reader.take(80)
    count = reader.compact()
    if not count or count > (len(data) - reader.pos) // 10:
        raise ValueError('Invalid transaction count')
    hashes = []
    for _ in range(count):
        start = reader.pos
        version = int.from_bytes(reader.take(4), 'little')
        inputs = reader.compact()
        if inputs > (len(data) - reader.pos) // 41:
            raise ValueError('Invalid input count')
        for _ in range(inputs):
            reader.take(36)
            reader.take(reader.compact())
            reader.take(4)
        outputs = reader.compact()
        if outputs > (len(data) - reader.pos) // 9:
            raise ValueError('Invalid output count')
        for _ in range(outputs):
            reader.take(8)
            reader.take(reader.compact())
        reader.take(4)
        if version & 0xffff == 3 and version >> 16 != 0:
            reader.take(reader.compact())
        hashes.append(sha256d(data[start:reader.pos]))
    if reader.pos != len(data):
        raise ValueError('Trailing block bytes')
    while len(hashes) > 1:
        if any(hashes[i] == hashes[i + 1] for i in range(0, len(hashes) - 1, 2)):
            raise ValueError('Mutated Merkle tree')
        if len(hashes) & 1:
            hashes.append(hashes[-1])
        hashes = [sha256d(hashes[i] + hashes[i + 1]) for i in range(0, len(hashes), 2)]
    if hashes[0] != header[36:68]:
        raise ValueError('Merkle root mismatch')
    return sha256d(header)


def canonical_chain(index, tip, height):
    chain, seen = [], set()
    current = bytes.fromhex(tip)[::-1]
    while current != bytes(32):
        if current in seen or current not in index:
            raise ValueError('Cyclic or incomplete ancestry')
        seen.add(current)
        record = index[current]
        chain.append((current, record))
        current = record[0]
    chain.reverse()
    if len(chain) != height + 1 or chain[0][0][::-1].hex() != GENESIS:
        raise ValueError('Height or BBC mainnet genesis mismatch')
    return chain


def file_hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def export(blocks, output, height, tip, delta_start):
    blocks = blocks.resolve(strict=True)
    output = output.resolve()
    if output == blocks or blocks in output.parents:
        raise ValueError('Output must be outside the source block directory')
    if not 0 < delta_start <= height:
        raise ValueError('Delta start must be within the pinned chain')
    output.mkdir(parents=True, exist_ok=False)
    paths = sorted(p for p in blocks.glob('blk[0-9][0-9][0-9][0-9][0-9].dat') if p.is_file() and not p.is_symlink())
    if not paths:
        raise ValueError('No raw block files')
    index, files, maps = {}, [], []
    try:
        for number, path in enumerate(paths):
            stream = path.open('rb')
            files.append(stream)
            mm = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
            maps.append(mm)
            pos = 0
            while pos < len(mm):
                if mm[pos:pos + 4] != MAGIC:
                    if not mm[pos:].strip(b'\0'):
                        break
                    raise ValueError('Unexpected frame magic in ' + path.name)
                if pos + 8 > len(mm):
                    raise ValueError('Truncated frame')
                size = struct.unpack_from('<I', mm, pos + 4)[0]
                if not 81 <= size <= MAX_BLOCK or pos + 8 + size > len(mm):
                    raise ValueError('Invalid frame size in ' + path.name)
                header = mm[pos + 8:pos + 88]
                key = sha256d(header)
                index.setdefault(key, (header[4:36], number, pos, size))
                pos += 8 + size
            print('Indexed {}: {} unique headers'.format(path.name, len(index)), flush=True)
        chain = canonical_chain(index, tip, height)
        full = output / 'bootstrap.dat'
        delta = output / 'bootstrap-update-{}-{}.dat'.format(delta_start, height)
        sample = output / 'validation-sample-1000.dat'
        with full.open('xb') as whole, delta.open('xb') as update, sample.open('xb') as first:
            for h, (key, (_, number, pos, size)) in enumerate(chain):
                frame = maps[number][pos:pos + 8 + size]
                if validate_block(frame[8:]) != key:
                    raise ValueError('Header mismatch at height {}'.format(h))
                whole.write(frame)
                if h >= delta_start:
                    update.write(frame)
                if h < 1000:
                    first.write(frame)
                if h % 100000 == 0:
                    print('Merkle-verified and exported height {}'.format(h), flush=True)
        common = dict(network='Babacoin mainnet', network_magic=MAGIC.hex(), genesis_hash=GENESIS,
                      tip_height=height, tip_hash=tip, format='mainnet magic + uint32 LE length + serialized block',
                      verification='Pinned cold-node ancestry, frame bounds, transaction framing and every transaction Merkle root. Not an independent full consensus replay. Normal node validation is required.',
                      exclusions=['wallets', 'keys', 'configuration', 'credentials', 'chainstate', 'block index', 'peers', 'PoW cache'])
        archives = []
        readme = ('# Babacoin mainnet bootstrap\n\n'
                  'This archive contains public raw blocks only. Normal proof-of-work, transaction and consensus validation still runs; this saves network downloading, not validation time.\n\n'
                  'Back up your wallet, stop Babacoin cleanly and extract the archive into a separate folder. Do not replace an existing data folder or wallet.dat. Check SHA256SUMS-BOOTSTRAP and the pinned height/hash in BOOTSTRAP-MANIFEST.json.\n\n'
                  'Automatic full import: put the extracted bootstrap.dat directly in the active mainnet data directory, next to blocks and chainstate, then restart your normal wallet shortcut. RC4 already supports automatic detection; V2.0.1 additionally displays actual file-read progress followed by block validation. Importing bootstrap.dat... in debug.log proves detection/start, not completion. A ZIP or a file in a different directory is not imported. Keep a separate copy of the archive.\n\n'
                  'Start your normal Babacoin executable with its normal data directory and the additional argument -loadblock="/absolute/path/to/the/extracted/file.dat". On Windows, quote the entire argument if its path contains spaces. Remove this argument after a successful import. The file can be imported again after an interruption; existing validated blocks are skipped.\n\n'
                  'The automatic input may be renamed bootstrap.dat.old after the read attempt. That name does not prove validation succeeded. After interruption, retry the preserved extracted file or .old file with -loadblock. Never delete wallets or chain data to restart importing. File-read 100% is not overall synchronization completion; let validation, rescanning and newer network blocks finish.\n\n'
                  'Full bootstrap starts at genesis. The update archive requires an already validated mainnet chain containing its required_base_height and required_base_hash. If unsure, use the full archive. Never import full and update together. Let the node synchronize newer blocks normally.\n\n'
                  'The incremental filename is not automatically detected: use its explicit -loadblock path after verifying the required base block. Verify the final validated height/hash against BOOTSTRAP-MANIFEST.json.\n\n'
                  'Use a current V2 node for current network rules. No trusted chainstate, private wallet data, keys, peer list or verification bypass is included. An Explorer cross-check of the pinned tip is additional evidence, not a substitute for validation.\n')
        (output / 'BOOTSTRAP-README.md').write_text(readme, encoding='utf-8')
        for payload, start, name in [(full, 0, 'bootstrap.zip'), (delta, delta_start, 'bootstrap-update-{}-{}.zip'.format(delta_start, height))]:
            record = dict(common, first_height=start, block_count=height - start + 1,
                          payload_name=payload.name, payload_bytes=payload.stat().st_size, payload_sha256=file_hash(payload))
            if start:
                record.update(required_base_height=start - 1, required_base_hash=chain[start - 1][0][::-1].hex())
            archive = output / name
            print('Compressing ' + name, flush=True)
            with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as z:
                z.write(payload, payload.name)
                z.writestr('CONTENTS.json', json.dumps(record, indent=2, sort_keys=True) + '\n')
                z.writestr('BOOTSTRAP-README.md', readme)
            if archive.stat().st_size >= 2 * 1024 ** 3:
                raise ValueError('Archive exceeds release asset limit')
            with zipfile.ZipFile(archive) as z:
                if z.testzip() is not None or set(z.namelist()) != {payload.name, 'CONTENTS.json', 'BOOTSTRAP-README.md'}:
                    raise ValueError('Archive integrity or allowlist failure')
            archives.append(dict(record, archive_name=name, archive_bytes=archive.stat().st_size, archive_sha256=file_hash(archive)))
        manifest = dict(common, archives=archives, exporter_sha256=file_hash(Path(__file__)),
                        sample=dict(block_count=min(1000, len(chain)), tip_hash=chain[min(999, height)][0][::-1].hex()))
        write_json(output / 'BOOTSTRAP-MANIFEST.json', manifest)
        names = [a['archive_name'] for a in archives] + ['BOOTSTRAP-MANIFEST.json', 'BOOTSTRAP-README.md']
        (output / 'SHA256SUMS-BOOTSTRAP').write_text(''.join('{}  {}\n'.format(file_hash(output / n), n) for n in names), encoding='ascii')
        print(json.dumps(dict(result='PASS', height=height, tip_hash=tip, archives=[a['archive_name'] for a in archives])), flush=True)
    finally:
        for mm in maps:
            mm.close()
        for stream in files:
            stream.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blocks', type=Path, required=True, help='Cold blocks directory from a cleanly stopped, validated node')
    parser.add_argument('--output', type=Path, required=True, help='New directory outside source blocks; must not exist')
    parser.add_argument('--tip-height', type=int, required=True)
    parser.add_argument('--tip-hash', required=True)
    parser.add_argument('--delta-start', type=int, required=True)
    args = parser.parse_args()
    export(args.blocks, args.output, args.tip_height, args.tip_hash, args.delta_start)
