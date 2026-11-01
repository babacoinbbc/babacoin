#!/usr/bin/env bash
# Download and verify a release for a FRESH installation. Never starts a node.
set -euo pipefail
umask 077
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
BBC_VERSION=${BBC_VERSION:-}
if [ -e "$HOME/.babacoin" ] || [ -e "$HOME/.babacoincore" ] \
   || command -v babacoind >/dev/null 2>&1 \
   || [ -e /etc/systemd/system/babacoind.service ] \
   || [ -e /lib/systemd/system/babacoind.service ] \
   || [ -e /usr/lib/systemd/system/babacoind.service ] \
   || { command -v pgrep >/dev/null 2>&1 && pgrep -x babacoind >/dev/null 2>&1; }; then
    die 'Existing Babacoin installation/data detected. Use doc/v2-migration.md; this helper never upgrades an existing node.'
fi
[ -n "$BBC_VERSION" ] || die 'Set BBC_VERSION to an explicitly reviewed published release tag.'
[[ "$BBC_VERSION" =~ ^v[0-9][A-Za-z0-9._-]*$ ]] || die 'Invalid release tag.'
[ "$(uname -s)" = Linux ] || die 'This download helper supports Linux only.'
[ "$EUID" -ne 0 ] || die 'Run as your regular user, not root.'
for tool in curl python3 sha256sum; do command -v "$tool" >/dev/null || die "Missing prerequisite: $tool"; done
root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
[ -f "$root/verify-release-archive.py" ] || die 'Clone the tagged source; the adjacent package verifier is required.'
[ -r /etc/os-release ] || die 'Cannot identify this distribution.'
# /etc/os-release is the local operating system identification file.
source /etc/os-release
case "${ID:-}:${VERSION_ID:-}" in
    ubuntu:18.04|ubuntu:20.04|ubuntu:22.04|ubuntu:24.04|ubuntu:26.04) distro=ubuntu; version=$VERSION_ID ;;
    debian:12|debian:13) distro=debian; version=$VERSION_ID ;;
    *) die 'No validated prebuilt package for this distribution/version. Do not substitute another distribution ABI.' ;;
esac
case "$(uname -m)" in
    x86_64) arch=x86_64 ;;
    aarch64) arch=aarch64 ;;
    *) die 'Only x86_64 and aarch64 packages are validated by this helper.' ;;
esac
name="babacoin-${BBC_VERSION}-${distro}-${version}-${arch}.tar.gz"
url="https://github.com/babacoinbbc/babacoin/releases/download/$BBC_VERSION/$name"
work=$(mktemp -d "${TMPDIR:-/tmp}/babacoin-install.XXXXXXXX")
cleanup() { case "$work" in "${TMPDIR:-/tmp}/babacoin-install."*) rm -rf -- "$work" ;; esac; }
trap cleanup EXIT
curl -fL --retry 3 --proto '=https' --tlsv1.2 "$url" -o "$work/package.tar.gz"
curl -fL --retry 3 --proto '=https' --tlsv1.2 "$url.sha256" -o "$work/package.sha256"
expected=$(awk 'NR==1 {print $1}' "$work/package.sha256")
[[ "$expected" =~ ^[a-fA-F0-9]{64}$ ]] || die 'Invalid checksum file.'
actual=$(sha256sum "$work/package.tar.gz" | awk '{print $1}')
[ "${expected,,}" = "$actual" ] || die 'Archive checksum mismatch; nothing installed.'
python3 "$root/verify-release-archive.py" "$work/package.tar.gz" "$work/unpacked" "$arch"
destination=${BBC_INSTALL_DIR:-"$HOME/.local/opt/babacoin/$BBC_VERSION"}
python3 - "$work/unpacked" "$destination" <<'PY'
import shutil, sys
from pathlib import Path
source, destination = map(Path, sys.argv[1:])
if not destination.is_absolute() or destination.exists() or destination.is_symlink():
    raise SystemExit('Choose a NEW absolute BBC_INSTALL_DIR; existing paths are never overwritten.')
destination.parent.mkdir(parents=True, exist_ok=True)
shutil.copytree(str(source), str(destination))
print('Verified package installed in: ' + str(destination))
PY
printf '%s\n' 'No wallet, configuration, firewall, service, OS security setting or running node was changed.'
printf '%s\n' 'Read doc/v2-migration.md and the packaged runtime-library list before starting the applications.'
