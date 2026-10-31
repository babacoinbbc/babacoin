#!/usr/bin/env python3
"""Copy recursive MinGW DLL imports; Windows system DLLs stay on Windows."""
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
directory = Path(sys.argv[2]).resolve()
available = {p.name.lower(): p for p in directory.glob("*.dll")}
queue = list(root.rglob("*.exe")) + list(root.rglob("*.dll"))
checked = set()
while queue:
    binary = queue.pop()
    if binary in checked:
        continue
    checked.add(binary)
    imports = subprocess.check_output(["objdump", "-p", str(binary)], text=True, errors="replace")
    for name in re.findall(r"DLL Name:\s*(\S+)", imports):
        source = available.get(name.lower())
        if source is not None:
            target = root / source.name
            if not target.exists():
                shutil.copy2(str(source), str(target))
                queue.append(target)
print("Resolved imports for %d PE files." % len(checked))
