#!/usr/bin/env python3
"""Offline daemon/wallet smoke test. Only freshly created temporary data is used.

Works on Windows and Linux, without third-party Python packages. This is not a
mainnet sync, consensus, GUI, or old/new binary interoperability certification.
"""
import argparse
import base64
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

MAIN_GENESIS = "84de4877419c696744198422de2628087ae2270c73fb370ab4cfe2fe01061854"
MAIN_GENESIS_POW = "000c5a0b4720b28e4e86a15a50f5358a9eee5f72127bf450eb479dd38ae6bcde"


class Node:
    def __init__(self, binary, datadir, regtest, wallet=True, local_peer=False):
        self.datadir = Path(datadir)
        self.datadir.mkdir(parents=True, exist_ok=True)
        # Never inherit the user's babacoin.conf, wallet, ports or RPC credentials.
        self.conf = self.datadir / "babacoin.conf"
        self.conf.write_text("", encoding="utf-8")
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.p2p_port = sock.getsockname()[1]
        self.cookie = self.datadir / ("regtest/.cookie" if regtest else ".cookie")
        self.log = open(self.datadir / "smoke-process.log", "ab")
        args = [str(binary), "-datadir=" + str(self.datadir), "-conf=" + str(self.conf),
                "-server=1", "-connect=0", "-dnsseed=0", "-discover=0",
                "-upnp=0", "-listenonion=0", "-par=1",
                "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1",
                "-rpcport=" + str(self.port), "-keypool=10"]
        if local_peer:
            args += ["-listen=1", "-bind=127.0.0.1", "-maxconnections=16",
                     "-port=" + str(self.p2p_port)]
        else:
            args += ["-listen=0", "-maxconnections=0"]
        if regtest:
            args.append("-regtest=1")
        if not wallet:
            args.append("-disablewallet=1")
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self.process = subprocess.Popen(args, stdout=self.log, stderr=self.log,
                                        creationflags=flags)
        try:
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise RuntimeError("daemon exited: " + str(self.datadir / "smoke-process.log"))
                try:
                    self.rpc("getblockchaininfo")
                    return
                except (OSError, ValueError, RuntimeError):
                    time.sleep(0.2)
            raise RuntimeError("RPC startup timeout: " + str(self.datadir))
        except BaseException:
            self.close()
            raise

    def rpc(self, method, *params, timeout=10):
        auth = base64.b64encode(self.cookie.read_bytes().strip()).decode("ascii")
        payload = json.dumps({"jsonrpc": "1.0", "id": "smoke", "method": method,
                              "params": list(params)}).encode("utf-8")
        request = urllib.request.Request("http://127.0.0.1:%d/" % self.port, payload,
                                         {"Authorization": "Basic " + auth,
                                          "Content-Type": "application/json"})
        # Ignore host HTTP proxy configuration for loopback RPC.
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            response = opener.open(request, timeout=timeout)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            result = json.load(response)
        if result.get("error"):
            raise RuntimeError(str(result["error"]))
        return result["result"]

    def close(self):
        try:
            if self.process.poll() is None:
                try:
                    self.rpc("stop")
                    self.process.wait(timeout=30)
                except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired):
                    self.process.terminate()
                    self.process.wait(timeout=15)
        finally:
            self.log.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(binary, work, wallet_network="regtest"):
    report = {"binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
              "workdir": str(work), "wallet_network": wallet_network, "checks": []}
    with Node(binary, work / "main-offline", False, wallet=False) as node:
        info = node.rpc("getblockchaininfo")
        net = node.rpc("getnetworkinfo")
        # A final-source build must not still identify itself as an RC at runtime.
        release_source = (Path(__file__).resolve().parents[2] / "configure.ac").read_text()
        version_parts = [int(re.search(r'define\(_CLIENT_VERSION_' + name + r', ([0-9]+)\)', release_source)[1])
                         for name in ('MAJOR', 'MINOR', 'REVISION', 'BUILD')]
        expected_version = sum(n*scale for n, scale in zip(version_parts, (1000000, 10000, 100, 1)))
        require(net['version'] == expected_version, 'runtime version contradicts configure.ac')
        require(net['subversion'] == '/Babacoin Core:' + '.'.join(map(str, version_parts)) + '/',
                'runtime user agent contradicts configure.ac')
        source_commit = os.environ.get('BBC_SOURCE_COMMIT') or os.environ.get('GITHUB_SHA')
        if source_commit:
            require(re.fullmatch('[a-f0-9]{40}', source_commit) is not None, 'invalid CI source commit')
            require(source_commit[:7] in net['buildversion'], 'runtime build identity contradicts CI source')
        if "define(_CLIENT_VERSION_IS_RELEASE, true)" in release_source:
            require("pre-release test build" not in net.get("warnings", ""),
                    "final-source binary still reports the prerelease warning")
            report["checks"].append("final binary has no prerelease warning at runtime")
        require(info["chain"] == "main", "unexpected main chain")
        require(node.rpc("getblockhash", 0) == MAIN_GENESIS, "mainnet genesis mismatch")
        powhash = node.rpc("getblock", MAIN_GENESIS, 1, True)["powhash"]
        require(powhash == MAIN_GENESIS_POW, "mainnet genesis GhostRider PoW mismatch")
        require(node.rpc("getconnectioncount") == 0, "unexpected P2P connection")
        require(not net["localaddresses"], "unexpected advertised network address")
        report.update(version=net["version"], buildversion=net["buildversion"], subversion=net["subversion"],
                      protocolversion=net["protocolversion"], genesis_powhash=powhash)
        report["checks"].append("offline mainnet identity and zero P2P connections")
    regtest = wallet_network == "regtest"
    walletdir = work / "wallet"
    backup = work / "rpc-backup.dat"
    mined_block = None
    immature_balance = None
    with Node(binary, walletdir, regtest) as node:
        require(node.rpc("getblockchaininfo")["chain"] == wallet_network, "unexpected test chain")
        node.rpc("getwalletinfo")
        address = node.rpc("getnewaddress", "isolated-smoke")
        require(node.rpc("getaddressinfo", address)["ismine"], "generated address not owned")
        if regtest:
            blocks = node.rpc("generatetoaddress", 1, address)
            require(len(blocks) == 1 and node.rpc("getblockcount") == 1, "regtest block generation failed")
            mined_block = node.rpc("getblock", blocks[0], 0)
            immature_balance = node.rpc("getwalletinfo")["immature_balance"]
            require(immature_balance > 0, "regtest coinbase was not credited to the test wallet")
            report["checks"].append("one isolated regtest block is mined and accepted")
        node.rpc("backupwallet", str(backup))
        require(backup.is_file() and backup.stat().st_size > 0, "RPC backup missing")
        require(node.rpc("getconnectioncount") == 0, "unexpected P2P connection")
        report["checks"].append("fresh isolated wallet, owned address, RPC backup, zero P2P connections")
    with Node(binary, walletdir, regtest) as node:
        require(node.rpc("getaddressinfo", address)["ismine"], "wallet restart lost address")
        if immature_balance is not None:
            require(node.rpc("getwalletinfo")["immature_balance"] == immature_balance,
                    "wallet restart changed the regtest coinbase balance")
        report["checks"].append("wallet ownership survives clean daemon restart")
    restored = work / "restored"
    restore_chain_dir = restored / "regtest" if regtest else restored
    restore_chain_dir.mkdir(parents=True)
    # Source is the closed RPC backup created by this test, never a live wallet.
    shutil.copyfile(backup, restore_chain_dir / "wallet.dat")
    with Node(binary, restored, regtest) as node:
        require(node.rpc("getaddressinfo", address)["ismine"], "backup restore lost address")
        if mined_block:
            require(node.rpc("submitblock", mined_block) is None, "regtest block replay failed")
            require(node.rpc("getblockcount") == 1, "restored node did not accept the original block")
            require(node.rpc("getwalletinfo")["immature_balance"] == immature_balance,
                    "restored wallet did not recover the regtest coinbase balance")
            report["checks"].append("original regtest block replays after wallet restore")
        report["checks"].append("RPC backup restores ownership in a separate data directory")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--daemon", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--wallet-network", choices=("regtest", "main"), default="regtest",
                        help="main permits offline testing of legacy releases with broken regtest")
    args = parser.parse_args()
    # Keep logs on failure for diagnosis; every run has an unpredictable new root.
    work = Path(tempfile.mkdtemp(prefix="babacoin-isolated-smoke-"))
    report = run(args.daemon.resolve(strict=True), work, args.wallet_network)
    output = json.dumps(report, indent=2)
    if args.report:
        args.report.write_text(output + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
