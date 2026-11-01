#!/usr/bin/env python3
"""Test old/new wallet ownership and localhost-only P2P using disposable data.

No existing wallet is ever opened. This does not prove historical sync,
activation-boundary agreement, transaction interoperability or fund safety.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from isolated_smoke import Node, MAIN_GENESIS, require


def run(old, new, work, encrypted=False):
    checks = []
    message = "Babacoin offline wallet compatibility check; not a transaction."
    wallet = work / "wallet-roundtrip"
    # Public, disposable test password; never supplied by or used for a user wallet.
    passphrase = "BBC isolated encryption compatibility test only"
    def unlock(node):
        if encrypted:
            node.rpc("walletpassphrase", passphrase, 60)
            require(node.rpc("getwalletinfo")["unlocked_until"] > 0, "encrypted test wallet did not unlock")
    with Node(old, wallet, False) as node:
        require(node.rpc("getblockhash", 0) == MAIN_GENESIS, "legacy mainnet mismatch")
        address = node.rpc("getnewaddress", "cross-version-test")
        old_signature = node.rpc("signmessage", address, message)
        if encrypted:
            node.rpc("encryptwallet", passphrase)
        else:
            node.rpc("backupwallet", str(work / "old-backup.dat"))
    require(node.process.returncode == 0, "legacy test wallet shutdown was not clean")
    if encrypted:
        with Node(old, wallet, False) as node:
            unlock(node)
            node.rpc("backupwallet", str(work / "old-backup.dat"))
        checks.append("legacy encrypted wallet unlocks after clean encryption shutdown")
    with Node(new, wallet, False) as node:
        unlock(node)
        require(node.rpc("getaddressinfo", address)["ismine"], "new binary lost old address")
        require(node.rpc("verifymessage", address, old_signature, message), "new binary rejects legacy signature")
        new_signature = node.rpc("signmessage", address, message)
        node.rpc("backupwallet", str(work / "new-backup.dat"))
        checks.append("new binary opens a fresh legacy wallet and preserves ownership")
    with Node(old, wallet, False) as node:
        unlock(node)
        require(node.rpc("getaddressinfo", address)["ismine"], "legacy binary cannot reopen upgraded wallet")
        require(node.rpc("verifymessage", address, new_signature, message), "legacy binary rejects new signature")
        checks.append("legacy binary reopens the wallet after new binary shutdown")
        checks.append("both binaries sign and verify with the same disposable private key")
    for name, binary in [("old-backup", new), ("new-backup", old)]:
        restored = work / (name + "-restored")
        restored.mkdir()
        shutil.copyfile(work / (name + ".dat"), restored / "wallet.dat")
        with Node(binary, restored, False) as node:
            unlock(node)
            require(node.rpc("getaddressinfo", address)["ismine"], "cross-version restore lost ownership")
            signature = node.rpc("signmessage", address, message)
            require(node.rpc("verifymessage", address, signature, message), "restored private key cannot sign")
        checks.append(name + " restores address ownership in the other binary")
    with Node(old, work / "old-peer", False, wallet=False, local_peer=True) as a:
        with Node(new, work / "new-peer", False, wallet=False, local_peer=True) as b:
            a.rpc("addnode", "127.0.0.1:%d" % b.p2p_port, "onetry")
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                ap, bp = a.rpc("getpeerinfo"), b.rpc("getpeerinfo")
                if ap and bp and ap[0].get("version", 0) and bp[0].get("version", 0):
                    break
                time.sleep(0.2)
            require(len(ap) == len(bp) == 1, "local P2P handshake failed")
            require(ap[0]["addr"].startswith("127.0.0.1:"), "non-loopback peer")
            require(bp[0]["addr"].startswith("127.0.0.1:"), "non-loopback peer")
            require(a.rpc("getblockhash", 0) == b.rpc("getblockhash", 0) == MAIN_GENESIS,
                    "peers use different genesis blocks")
            checks.append("old/new P2P handshake succeeds using localhost only")
    return {"old_sha256": hashlib.sha256(old.read_bytes()).hexdigest(),
            "new_sha256": hashlib.sha256(new.read_bytes()).hexdigest(),
            "workdir": str(work), "encrypted": encrypted, "checks": checks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old-daemon", type=Path, required=True)
    parser.add_argument("--new-daemon", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--encrypted", action="store_true", help="encrypt only the disposable legacy wallet and test unlock/restore")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="babacoin-cross-version-"))
    result = run(args.old_daemon.resolve(strict=True), args.new_daemon.resolve(strict=True), work, args.encrypted)
    output = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.write_text(output, encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
