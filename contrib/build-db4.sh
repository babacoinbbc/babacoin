#!/usr/bin/env bash
# Build wallet-compatible BDB 4.8 into an explicit local prefix. No sudo.
set -euo pipefail
if [ "$#" -ne 1 ] || [[ "$1" != /* ]] || [[ "$1" =~ [[:space:]] ]]; then
    echo "Usage: bash contrib/build-db4.sh /absolute/prefix-without-spaces" >&2
    exit 2
fi
prefix=$1
repo=$(cd "$(dirname "$0")/.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/babacoin-bdb4.XXXXXXXX")
trap 'rm -rf -- "$build"' EXIT
cd "$build"
curl --fail --location --retry 3 --proto '=https' --tlsv1.2 \
    https://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz -o db.tar.gz
echo '12edc0df75bf9abd7f82f821795bcee50f42cb2e5f76a6a281b85732798364ef  db.tar.gz' | sha256sum -c -
tar -xzf db.tar.gz
cd db-4.8.30.NC
patch -p1 < "$repo/depends/patches/bdb/clang_cxx_11.patch"
# BDB's original 2010 platform scripts predate AArch64. Use the same maintained
# scripts as the existing depends recipe, including for native ARM64 builds.
chmod u+w dist/config.guess dist/config.sub
cp "$repo/depends/config.guess" "$repo/depends/config.sub" dist/
cd build_unix
configure_args=(--prefix="$prefix" --disable-shared --enable-cxx --disable-replication)
case "$(uname -s)" in
    MINGW*|MSYS*) configure_args+=(--host="$(gcc -dumpmachine)" --enable-mingw) ;;
esac
CFLAGS="${CFLAGS:--O2 -fPIC -std=gnu11 -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=incompatible-pointer-types}" \
CXXFLAGS="${CXXFLAGS:--O2 -fPIC -std=c++17}" \
    ../dist/configure "${configure_args[@]}"
make -j"${JOBS:-2}" libdb_cxx-4.8.a libdb-4.8.a
make install_lib install_include
echo "BDB_CFLAGS=-I$prefix/include"
echo "BDB_LIBS=-L$prefix/lib -ldb_cxx-4.8"
