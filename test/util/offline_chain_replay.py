#!/usr/bin/env python3
"""Compare historical raw blocks with two binaries, in new offline data dirs.

Never imports a snapshot's wallet, configuration, index, chainstate or peers.
All consensus validation is performed by each daemon with assumevalid disabled.
The structural header scan is only an expected-tip check, not validation.
"""
import argparse
from collections import defaultdict, deque
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from isolated_smoke import MAIN_GENESIS, Node, require


def scan(files, sample_dir=None, max_height=None):
    headers = {}
    digests = {}
    for path in files:
        file_size = path.stat().st_size
        digests[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
        with path.open("rb") as stream:
            while True:
                prefix = stream.read(8)
                if not prefix or not prefix.strip(b"\0"):
                    break  # preallocated unused file tail
                require(prefix[:4] == bytes.fromhex("035dd72f"), "unexpected block magic in " + str(path))
                size = int.from_bytes(prefix[4:], "little")
                require(80 <= size <= 32 * 1024 * 1024, "invalid raw block size")
                header = stream.read(80)
                require(len(header) == 80, "truncated header")
                blockhash = hashlib.sha256(hashlib.sha256(header).digest()).digest()[::-1].hex()
                previous = header[4:36][::-1].hex()
                bits = int.from_bytes(header[72:76], "little")
                exponent, mantissa = bits >> 24, bits & 0x7fffff
                target = mantissa << (8 * (exponent - 3)) if exponent >= 3 else mantissa >> (8 * (3 - exponent))
                require(not bits & 0x800000 and 0 < target < 2 ** 256, "invalid structural target")
                headers[blockhash] = (previous, (2 ** 256) // (target + 1))
                stream.seek(size - 80, 1)
                require(stream.tell() <= file_size, "truncated block")
    require(MAIN_GENESIS in headers, "archive lacks BBC genesis")
    resolved = {MAIN_GENESIS: (0, headers[MAIN_GENESIS][1])}
    children = defaultdict(list)
    for blockhash, (previous, work) in headers.items():
        if blockhash != MAIN_GENESIS:
            children[previous].append(blockhash)
    pending = deque([MAIN_GENESIS])
    while pending:
        previous = pending.popleft()
        height, parentwork = resolved[previous]
        for blockhash in children.pop(previous, ()):
            resolved[blockhash] = (height + 1, parentwork + headers[blockhash][1])
            pending.append(blockhash)
    require(len(resolved) == len(headers), "archive contains headers with missing ancestors")
    tip = max(resolved, key=lambda h: resolved[h][1])
    result = {"archive_tip": tip, "archive_height": resolved[tip][0],
              "unique_archive_headers": len(headers), "raw_sha256": digests}
    if max_height is not None:
        require(0 < max_height <= resolved[tip][0], "sample height outside archive")
        while resolved[tip][0] > max_height:
            tip = headers[tip][0]
        selected = set()
        current = tip
        while current in headers:
            selected.add(current)
            current = headers[current][0]
        sample_dir.mkdir()
        sample = sample_dir / "blk00000.dat"
        copied = 0
        with sample.open("xb") as output:
            for path in files:
                with path.open("rb") as stream:
                    while True:
                        prefix = stream.read(8)
                        if not prefix or not prefix.strip(b"\0"):
                            break
                        size = int.from_bytes(prefix[4:], "little")
                        header = stream.read(80)
                        blockhash = hashlib.sha256(hashlib.sha256(header).digest()).digest()[::-1].hex()
                        if blockhash in selected:
                            output.write(prefix + header + stream.read(size - 80))
                            selected.remove(blockhash)
                            copied += 1
                        else:
                            stream.seek(size - 80, 1)
        require(not selected and copied == max_height + 1, "incomplete sample extraction")
        files = [sample]
        result["sample_only"] = True
    result.update(expected_tip=tip, expected_height=resolved[tip][0])
    return result, files


def replay(binary, files, work, expected, timeout):
    work.mkdir()
    blocks = work / "blocks"
    blocks.mkdir()
    # Independent copies: the daemon can append to these files while reindexing.
    # Hard links would modify the reference archive and the other test's input.
    for path in files:
        shutil.copyfile(path, blocks / path.name)
    conf = work / "babacoin.conf"
    conf.write_text("", encoding="utf-8")
    args = [str(binary), "-datadir=" + str(work), "-conf=" + str(conf),
            "-disablewallet=1", "-connect=0", "-listen=0", "-dnsseed=0",
            "-discover=0", "-upnp=0", "-listenonion=0", "-maxconnections=0",
            "-par=2", "-assumevalid=0", "-reindex=1", "-stopafterblockimport=1"]
    start = time.monotonic()
    with (work / "import-process.log").open("wb") as log:
        process = subprocess.Popen(args, stdout=log, stderr=log)
        try:
            require(process.wait(timeout=timeout) == 0, "block import process failed: " + str(work))
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=30)
    with Node(binary, work, False, wallet=False) as node:
        info = node.rpc("getblockchaininfo")
        require(node.rpc("getconnectioncount") == 0, "unexpected P2P connection")
        require(info["blocks"] == expected["expected_height"], "not all archived blocks accepted: " + str(info))
        require(info["bestblockhash"] == expected["expected_tip"], "archive tip mismatch")
        utxo = node.rpc("gettxoutsetinfo", timeout=600)
    return {"binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "seconds": round(time.monotonic() - start, 1), "blockchain": info, "utxo": utxo}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-blocks", type=Path, required=True)
    parser.add_argument("--old-daemon", type=Path, required=True)
    parser.add_argument("--new-daemon", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=7200)
    parser.add_argument("--max-height", type=int,
                        help="validate only this canonical archive prefix; report marks it as a sample")
    parser.add_argument("--reuse-old-report", type=Path,
                        help="reuse a completed old-binary replay with identical binary/input hashes and tip")
    args = parser.parse_args()
    files = sorted(path.resolve(strict=True) for path in args.raw_blocks.iterdir()
                   if re.fullmatch(r"blk[0-9]{5}\.dat", path.name) and path.is_file())
    require(bool(files), "no raw block files")
    work = Path(tempfile.mkdtemp(prefix="babacoin-chain-replay-"))
    result, files = scan(files, work / "sample-raw", args.max_height)
    result["workdir"] = str(work)
    print(json.dumps(result, indent=2), flush=True)
    if args.reuse_old_report:
        previous = json.loads(args.reuse_old_report.read_text(encoding="utf-8"))
        for key in ("expected_tip", "expected_height", "raw_sha256", "archive_tip", "archive_height"):
            require(previous[key] == result[key], "old report uses different input: " + key)
        require(previous["old"]["binary_sha256"] == hashlib.sha256(args.old_daemon.read_bytes()).hexdigest(),
                "old report binary hash mismatch")
        require(previous["old"]["utxo"]["height"] == result["expected_height"] and
                previous["old"]["utxo"]["bestblock"] == result["expected_tip"], "incomplete old replay")
        result["old"] = previous["old"]
        result["old_reused_from"] = str(args.reuse_old_report.resolve())
    for name, binary in (("old", args.old_daemon), ("new", args.new_daemon)):
        if name == "old" and name in result:
            continue
        result[name] = replay(binary.resolve(strict=True), files, work / name, result, args.timeout)
        args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(name + " replay completed", flush=True)
    for key in ("height", "bestblock", "transactions", "txouts", "hash_serialized_2", "total_amount"):
        require(result["old"]["utxo"][key] == result["new"]["utxo"][key], "old/new UTXO mismatch: " + key)
    result["matching_utxo"] = True
    args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("Old/new historical chain tip and complete UTXO set match.")


if __name__ == "__main__":
    main()
