#!/usr/bin/env bash
# Native UCRT64 build in a disposable GitHub Windows runner.
set -euo pipefail
test "${GITHUB_ACTIONS:-false}" = true
test "${MSYSTEM:-}" = UCRT64
output=$PWD/desktop-output
mkdir -p "$output"
printf '%s\n' "$GITHUB_SHA" > "$output/SOURCE_COMMIT"
pacman -Q > "$output/packages.txt"
gcc --version > "$output/build-environment.txt"
export JOBS=2
export CMAKE_GENERATOR=Ninja
export CMAKE_PREFIX_PATH=$MINGW_PREFIX
deps=$PWD/local-deps
bash contrib/build-db4.sh "$deps"
bash ci/build-bls.sh "$deps"
./autogen.sh
./configure --host="$(gcc -dumpmachine)" --with-gui=qt5 --enable-tests \
    --disable-bench --disable-stacktraces --disable-shared \
    --with-boost="$MINGW_PREFIX" \
    BDB_CFLAGS="-I$deps/include" BDB_LIBS="-L$deps/lib -ldb_cxx-4.8" \
    CPPFLAGS="-I$deps/include" LDFLAGS="-L$deps/lib"
trap 'result=$?; test ! -f config.log || cp config.log "$output/"; exit "$result"' EXIT
make -j"$JOBS"
make -C src -j"$JOBS" test/test_babacoin.exe qt/test/test_babacoin-qt.exe
make -C src/secp256k1 -j"$JOBS" tests.exe exhaustive_tests.exe
make -C src/univalue -j"$JOBS" test/object.exe test/unitester.exe test/no_nul.exe
cat > "$output/bdb-version.c" <<'EOF'
#include <db.h>
#include <stdio.h>
int main(void) { int major, minor, patch; db_version(&major, &minor, &patch); printf("%d.%d.%d\n", major, minor, patch); return 0; }
EOF
gcc -I"$deps/include" "$output/bdb-version.c" -L"$deps/lib" -ldb-4.8 -o src/bdb-version.exe
for name in src/babacoind.exe src/babacoin-cli.exe src/babacoin-tx.exe src/bdb-version.exe \
    src/qt/babacoin-qt.exe src/test/test_babacoin.exe src/qt/test/test_babacoin-qt.exe \
    src/secp256k1/tests.exe src/secp256k1/exhaustive_tests.exe \
    src/univalue/test/object.exe src/univalue/test/unitester.exe src/univalue/test/no_nul.exe; do
    mkdir -p "$output/$(dirname "$name")"
    cp "$name" "$output/$name"
done
# Resolve all non-system imports recursively, including test executables.
python3 ci/windows-bundle-dlls.py "$output/src" "$MINGW_PREFIX/bin"
mkdir -p "$output/src/platforms"
cp "$MINGW_PREFIX/share/qt5/plugins/platforms/"q{minimal,windows}.dll "$output/src/platforms/"
python3 ci/windows-bundle-dlls.py "$output/src" "$MINGW_PREFIX/bin"
(cd "$output" && find src -type f -exec sha256sum {} + > SHA256SUMS)
