#!/usr/bin/env python3
"""Package this checkout, including edits, with Git's Unix modes/line endings.

Useful when preparing Linux build inputs on Windows or a network share. Only
tracked and unignored source files are included, never .git or ignored wallets.
"""
import argparse
import io
from pathlib import Path
import subprocess
import tarfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    git = ["git", "-C", str(root)]
    entries = subprocess.check_output(git + ["ls-files", "--stage", "-z"]).split(b"\0")
    modes = {}
    for entry in filter(None, entries):
        metadata, name = entry.split(b"\t", 1)
        modes[name.decode("utf-8")] = int(metadata.split()[0], 8) & 0o777
    lines = subprocess.check_output(git + ["ls-files", "--eol", "-z"]).split(b"\0")
    text_paths = set()
    for entry in filter(None, lines):
        metadata, name = entry.split(b"\t", 1)
        if metadata.split()[0] in (b"i/lf", b"i/crlf", b"i/mixed"):
            text_paths.add(name.decode("utf-8"))
    new = subprocess.check_output(git + ["ls-files", "--others", "--exclude-standard", "-z"])
    for name in filter(None, new.decode("utf-8").split("\0")):
        modes[name] = 0o755 if name.endswith(".sh") else 0o644
        if name.endswith((".sh", ".py", ".yml", ".json", ".md", ".cpp")):
            text_paths.add(name)
    output = args.output.resolve()
    if output == root or root in output.parents:
        parser.error("write the archive outside the source checkout")
    with output.open("xb") as stream, tarfile.open(fileobj=stream, mode="w") as archive:
        for name, mode in sorted(modes.items()):
            path = root / name
            if not path.is_file():
                continue
            data = path.read_bytes()
            if name in text_paths:
                data = data.replace(b"\r\n", b"\n")
            info = tarfile.TarInfo(name)
            info.mode = mode
            info.size = len(data)
            info.mtime = int(path.stat().st_mtime)
            archive.addfile(info, io.BytesIO(data))
    print(output)


if __name__ == "__main__":
    main()
