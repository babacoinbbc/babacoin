#!/usr/bin/env bash
# An offline, disposable GUI preview. Never imports existing wallet data.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
if test -x "$root/Babacoin-Qt.app/Contents/MacOS/Babacoin-Qt"; then
    binary="$root/Babacoin-Qt.app/Contents/MacOS/Babacoin-Qt"
elif test -x "$root/babacoin-qt"; then
    binary="$root/babacoin-qt"
elif test -x "$root/bin/babacoin-qt"; then
    binary="$root/bin/babacoin-qt"
else
    echo 'Cannot find the packaged Babacoin Qt executable.' >&2
    exit 1
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/babacoin-ui-qa.XXXXXXXX")
mkdir "$work/data" "$work/gui-settings"
: > "$work/data/babacoin.conf"
printf 'Disposable regtest data and GUI settings: %s\n' "$work"
exec "$binary" -regtest=1 "-datadir=$work/data" \
    "-conf=$work/data/babacoin.conf" "-uiconfigdir=$work/gui-settings" \
    -choosedatadir=0 -connect=0 -listen=0 -dnsseed=0 -discover=0 \
    -upnp=0 -listenonion=0 -maxconnections=0 -enablecoinjoin=0 \
    -windowtitle=Isolated-QA
