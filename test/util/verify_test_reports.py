#!/usr/bin/env python3
"""Reject incomplete test runs, including a premature exit with status zero."""
import argparse
from pathlib import Path
import re


def verify(root):
    suites = [root / "test/test_babacoin", root / "qt/test/test_babacoin-qt"]
    for suite in suites:
        status = suite.with_suffix(".trs").read_text(encoding="utf-8")
        log = suite.with_suffix(".log").read_text(encoding="utf-8", errors="replace")
        if ":test-result: PASS" not in status or ":global-test-result: PASS" not in status:
            raise ValueError("Failed Automake report: " + str(suite))
        if suite.name == "test_babacoin":
            if "*** No errors detected" not in log or re.search(r"(?:fatal error|error: in|failure is detected)", log):
                raise ValueError("Incomplete or failing Boost tests: " + str(suite))
        else:
            if "FAIL!" in log or "QFATAL" in log:
                raise ValueError("Failing Qt tests: " + str(suite))
            for name in ("URITests", "PaymentServerTests", "RPCNestedTests", "CompatTests", "WalletTests", "TrafficGraphDataTests"):
                if "Finished testing of " + name not in log:
                    raise ValueError("Incomplete Qt suite: " + name)
    print("Core and all six Qt suites completed successfully.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, default=Path("src"))
    verify(parser.parse_args().src)
