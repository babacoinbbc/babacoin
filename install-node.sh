#!/usr/bin/env bash
# Ubuntu/Debian mainnet node setup. Download this file, inspect it, then run it.
set -euo pipefail
umask 077

bbc_here=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
if [[ ${1:-} == --help ]]; then
    printf '%s\n' 'Usage: bash install-node.sh [--bootstrap auto|never|always] [--data-dir PATH] [--install-root PATH] [--no-start] [--plan]'
    printf '%s\n' 'Latest stable release; exact Ubuntu/Debian ABI; graceful daemon upgrade; no wallet/chain deletion.'
    exit 0
fi
[[ $(uname -s) == Linux ]] || { echo 'Linux is required.' >&2; exit 1; }
[[ -r /etc/os-release ]] || { echo 'Cannot identify the operating system.' >&2; exit 1; }
source /etc/os-release
case "${ID:-}:${VERSION_ID:-}" in
    ubuntu:18.04|ubuntu:20.04|ubuntu:22.04|ubuntu:24.04|ubuntu:26.04|debian:12|debian:13) ;;
    *) echo 'No validated release package for this OS/version. Another distribution ABI will not be substituted.' >&2; exit 1 ;;
esac
case $(uname -m) in x86_64|aarch64) ;; *) echo 'Only x86_64 and aarch64 are supported.' >&2; exit 1 ;; esac
if [[ $EUID -ne 0 && ${1:-} != --plan ]]; then
    exec sudo bash "$bbc_here/$(basename -- "${BASH_SOURCE[0]}")" "$@"
fi
if ! command -v python3 >/dev/null || ! command -v curl >/dev/null || [[ ! -r /etc/ssl/certs/ca-certificates.crt ]]; then
    [[ ${1:-} != --plan ]] || { echo 'Python 3 and curl are required for --plan.' >&2; exit 1; }
    apt-get update
    apt-get install -y --no-install-recommends python3 curl ca-certificates
fi
if [[ -f "$bbc_here/contrib/linux-node-setup.py" && -f "$bbc_here/contrib/verify-release-archive.py" ]]; then
    exec python3 "$bbc_here/contrib/linux-node-setup.py" "$@"
fi
# Standalone entry point: resolve main once, then fetch BOTH helpers from that
# immutable source commit. Never mix helpers from moving branch revisions.
bbc_tmp=$(mktemp -d "${TMPDIR:-/tmp}/bbc-node-setup.XXXXXXXX")
trap 'rm -rf -- "$bbc_tmp"' EXIT
bbc_source=$(curl -fsSL --proto '=https' --proto-redir '=https' --tlsv1.2 \
    https://api.github.com/repos/babacoinbbc/babacoin/commits/main \
    | python3 -c 'import json,sys; print(json.load(sys.stdin)["sha"])')
[[ $bbc_source =~ ^[a-f0-9]{40}$ ]] || { echo 'Invalid setup source identity.' >&2; exit 1; }
for bbc_file in linux-node-setup.py verify-release-archive.py; do
    curl -fSL --retry 3 --proto '=https' --proto-redir '=https' --tlsv1.2 \
        "https://raw.githubusercontent.com/babacoinbbc/babacoin/$bbc_source/contrib/$bbc_file" -o "$bbc_tmp/$bbc_file"
done
python3 "$bbc_tmp/linux-node-setup.py" "$@"
