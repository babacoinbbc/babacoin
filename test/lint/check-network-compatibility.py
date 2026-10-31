#!/usr/bin/env python3
"""Reject unreviewed changes to the audited mainnet source baseline.

This is a source-integrity tripwire, not proof of consensus equivalence or
security. The network baseline is main@7cdd64b, NOT the older v.1.0.0 release.
The BLS wrapper, disk decoder, DKG peer attribution, CLI stdout and startup
rescan checkpoint handling have recorded fixes. Five explicit V2 payment-boundary
exceptions implement the owner's mandatory-upgrade policy, not V1 equivalence.
A vendor test expectation for OpenSSL 3 is separately recorded. Never
regenerate it as a routine fix for a failing check: review network impact.
"""
import hashlib
import json
from pathlib import Path
import sys


def normalized_source(path, scope):
    text = path.read_text(encoding="utf-8")
    if scope == "excluding-regtest":
        start = text.index("class CRegTestParams : public CChainParams")
        end = text.index("static std::unique_ptr<CChainParams> globalChainParams;", start)
        text = text[:start] + text[end:]
    return text.encode("utf-8")


def main():
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root / "test/lint/network-baseline.json").read_text(encoding="utf-8"))
    failures = []
    for entry in manifest["files"]:
        path = root / entry["path"]
        try:
            digest = hashlib.sha256(normalized_source(path, entry["scope"])).hexdigest()
        except (OSError, ValueError) as error:
            failures.append("%s: %s" % (entry["path"], error))
            continue
        if digest != entry["sha256"]:
            failures.append(entry["path"] + ": changed from audited network baseline")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print("PASS: %d network/crypto/wallet source files match the recorded baseline (%s; BLS, disk, DKG, CLI-output, vendor-test, startup-rescan and V2 payment-boundary exceptions recorded explicitly)" %
          (len(manifest["files"]), manifest["baseline_commit"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
