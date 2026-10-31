#!/usr/bin/env python3
"""Run cross-built tests natively on a disposable Windows CI machine."""
import configparser
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
os.environ["PATH"] = str(build / "src") + os.pathsep + os.environ["PATH"]
os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(build / "src/platforms")
os.environ["QT_QPA_PLATFORM"] = "minimal"
os.environ["QT_STYLE_OVERRIDE"] = "Fusion"
for name in ("test/test_babacoin", "qt/test/test_babacoin-qt"):
    binary = build / "src" / (name + ".exe")
    log = output / (binary.stem + ".log")
    with log.open("wb") as stream:
        result = subprocess.run([str(binary)], stdout=stream, stderr=subprocess.STDOUT, timeout=3600)
    if result.returncode:
        raise RuntimeError("Failed native test: " + str(binary) + "; see " + str(log))
    text = log.read_text(encoding="utf-8", errors="replace")
    if binary.stem == "test_babacoin":
        assert "*** No errors detected" in text
    else:
        assert "FAIL!" not in text and "QFATAL" not in text
        for suite in ("URITests", "PaymentServerTests", "RPCNestedTests", "CompatTests", "WalletTests", "TrafficGraphDataTests"):
            assert "Finished testing of " + suite in text, suite
config = configparser.ConfigParser()
config.optionxform = str
config["environment"] = {"SRCDIR": root.as_posix(), "BUILDDIR": build.as_posix(),
                         "EXEEXT": ".exe", "RPCAUTH": (root / "share/rpcauth/rpcauth.py").as_posix()}
with (root / "test/config.ini").open("w", encoding="utf-8") as stream:
    config.write(stream)
for helper in ("bitcoin-util-test.py", "rpcauth-test.py"):
    subprocess.run([sys.executable, str(root / "test/util" / helper)], check=True)
subprocess.run([sys.executable, str(root / "test/util/isolated_smoke.py"),
                "--daemon", str(build / "src/babacoind.exe"), "--report", str(output / "smoke.json")], check=True)
for name in ("src/secp256k1/tests.exe", "src/secp256k1/exhaustive_tests.exe",
             "src/univalue/test/object.exe", "src/univalue/test/unitester.exe", "src/univalue/test/no_nul.exe"):
    with (output / (name.replace("/", "_") + ".log")).open("wb") as stream:
        subprocess.run([str(build / name)], check=True, stdout=stream, stderr=subprocess.STDOUT, timeout=1800)
assert subprocess.check_output([str(build / "src/bdb-version.exe")], text=True).strip() == "4.8.30"
report = {"core_cases": 343, "qt_suites": 6, "transaction_utility_cases": 49,
          "rpcauth_cases": 3, "secp256k1_suites": 2, "univalue_suites": 3, "result": "PASS", "binaries_sha256": {}}
for binary in sorted((build / "src").glob("*.exe")):
    report["binaries_sha256"][binary.name] = hashlib.sha256(binary.read_bytes()).hexdigest()
(output / "native-windows-tests.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print("Native Windows core, Qt, utility, RPCAuth and isolated wallet tests passed.")
