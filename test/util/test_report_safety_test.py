#!/usr/bin/env python3
"""Regression checks for the previously observed false-green test results."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("reports", Path(__file__).with_name("verify_test_reports.py"))
reports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reports)


class ReportSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.core = self.root / "test/test_babacoin"
        self.qt = self.root / "qt/test/test_babacoin-qt"
        for suite in (self.core, self.qt):
            suite.parent.mkdir(parents=True)
            suite.with_suffix(".trs").write_text(":test-result: PASS\n:global-test-result: PASS\n", encoding="utf-8")
        self.core.with_suffix(".log").write_text("*** No errors detected\n", encoding="utf-8")
        self.qt.with_suffix(".log").write_text("\n".join("Finished testing of " + name for name in
            ("URITests", "PaymentServerTests", "RPCNestedTests", "CompatTests", "WalletTests", "TrafficGraphDataTests")), encoding="utf-8")

    def test_complete_reports(self):
        with contextlib.redirect_stdout(io.StringIO()):
            reports.verify(self.root)

    def test_premature_success_exit(self):
        self.core.with_suffix(".log").write_text("Running 341 test cases...\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            reports.verify(self.root)

    def test_error_despite_success_status(self):
        self.core.with_suffix(".log").write_text('error: in test\n*** No errors detected\n', encoding="utf-8")
        with self.assertRaises(ValueError):
            reports.verify(self.root)

    def test_missing_final_qt_suite(self):
        self.qt.with_suffix(".log").write_text("Finished testing of WalletTests\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            reports.verify(self.root)

    def test_failed_automake_status(self):
        self.core.with_suffix(".trs").write_text(":test-result: FAIL\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            reports.verify(self.root)


if __name__ == "__main__":
    unittest.main()
