#!/usr/bin/env python3
"""Exercise backup failure paths with a fake CLI, never a real wallet/node."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class BackupSafetyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="bbc-backup-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.home = self.root / "home"
        self.home.mkdir()
        self.bindir = self.root / "bin"
        self.bindir.mkdir()
        # A sentinel demonstrates that the unsafe fallback never copies it.
        wallet = self.home / ".babacoin" / "wallet.dat"
        wallet.parent.mkdir()
        wallet.write_bytes(b"LIVE-WALLET-MUST-NOT-BE-COPIED")
        cli = self.bindir / "babacoin-cli"
        cli_text = '''#!/usr/bin/env bash
printf '%s\n' "$1" >> "$HOME/rpc-calls"
case "$1" in
  getblockchaininfo) [ "$BBC_MOCK_MODE" != offline ] ;;
  backupwallet)
    [ "$BBC_MOCK_MODE" = success ] || exit 1
    printf 'FLUSHED-RPC-BACKUP' > "$2"
    ;;
  *) exit 1 ;;
esac
'''
        with cli.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(cli_text)
        cli.chmod(0o755)
        source = Path(__file__).resolve().parents[2] / "contrib" / "prepare-wallet.sh"
        self.script = self.root / "prepare-wallet.sh"
        # Windows Git checkouts can use CRLF; Bash always needs LF.
        with self.script.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(source.read_text(encoding="utf-8"))
        self.bash = os.environ.get("BBC_TEST_BASH") or shutil.which("bash")
        if not self.bash:
            self.fail("Bash is required; set BBC_TEST_BASH to Git Bash on Windows")

    def invoke(self, mode):
        env = dict(os.environ, HOME=self.home.as_posix(), BBC_MOCK_MODE=mode)
        env["PATH"] = str(self.bindir) + os.pathsep + env["PATH"]
        return subprocess.run([self.bash, "--noprofile", "--norc", str(self.script), "backup"],
                              env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              encoding="utf-8", errors="replace", timeout=20)

    def invoke_installer(self):
        source = Path(__file__).resolve().parents[2] / "contrib/install.sh"
        script = self.root / "install.sh"
        with script.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write(source.read_text(encoding="utf-8"))
        env = dict(os.environ, HOME=self.home.as_posix(), BBC_VERSION="", USER="auditor")
        env["PATH"] = str(self.bindir) + os.pathsep + env["PATH"]
        return subprocess.run([self.bash, "--noprofile", "--norc", str(script)],
                              env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              encoding="utf-8", errors="replace", timeout=20)

    def test_installer_refuses_existing_data_before_changes(self):
        result = self.invoke_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Existing Babacoin installation/data detected", result.stderr)
        self.assertEqual((self.home / ".babacoin/wallet.dat").read_bytes(),
                         b"LIVE-WALLET-MUST-NOT-BE-COPIED")
        self.assertEqual(list(self.home.glob("bbc-install-*")), [])
        self.assertFalse((self.home / "rpc-calls").exists())

    def test_installer_has_no_implicit_version_upgrade(self):
        (self.home / ".babacoin").rename(self.home / "sentinel-original-data")
        result = self.invoke_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Set BBC_VERSION", result.stderr)
        self.assertEqual(list(self.home.glob("bbc-install-*")), [])

    def test_installer_refuses_a_running_custom_node(self):
        (self.home / ".babacoin").rename(self.home / "sentinel-original-data")
        command = self.bindir / "pgrep"
        with command.open("w", encoding="utf-8", newline="\n") as stream:
            stream.write("#!/usr/bin/env bash\nexit 0\n")
        command.chmod(0o755)
        result = self.invoke_installer()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Existing Babacoin installation/data detected", result.stderr)
        self.assertEqual(list(self.home.glob("bbc-install-*")), [])

    def test_running_node_with_failed_rpc_never_copies_wallet(self):
        result = self.invoke("rpc-fails")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn("No file-copy fallback", result.stderr)
        self.assertEqual(list(self.home.glob("babacoin-backups/*.dat")), [])
        self.assertIn("backupwallet", (self.home / "rpc-calls").read_text())

    def test_offline_node_does_not_claim_a_backup(self):
        result = self.invoke("offline")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(list(self.home.glob("babacoin-backups/*.dat")), [])
        self.assertNotIn("backupwallet", (self.home / "rpc-calls").read_text())

    def test_success_uses_rpc_result_and_private_permissions(self):
        result = self.invoke("success")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        files = list(self.home.glob("babacoin-backups/*.dat"))
        self.assertEqual(len(files), 1)
        self.assertEqual(files[0].read_bytes(), b"FLUSHED-RPC-BACKUP")
        if os.name != "nt":
            self.assertEqual(files[0].stat().st_mode & 0o777, 0o400)


if __name__ == "__main__":
    unittest.main()
