#!/usr/bin/env bash
# Disposable native GitHub macOS runner only; never install on a user's node.
set -euo pipefail
test "${GITHUB_ACTIONS:-false}" = true
output=$PWD/desktop-output
mkdir -p "$output"
printf '%s\n' "$GITHUB_SHA" > "$output/SOURCE_COMMIT"
uname -a > "$output/build-environment.txt"
brew install autoconf automake libtool pkg-config openssl@3 libevent boost \
    miniupnpc zeromq cmake gmp qt@5 protobuf libqrencode coreutils gnu-sed
p=$(brew --prefix)
export PATH="$p/opt/coreutils/libexec/gnubin:$p/opt/gnu-sed/libexec/gnubin:$p/opt/qt@5/bin:$PATH"
export CMAKE_PREFIX_PATH="$p/opt/gmp:$p"
export PKG_CONFIG_PATH="$p/opt/openssl@3/lib/pkgconfig:$p/opt/qt@5/lib/pkgconfig:$p/lib/pkgconfig"
export JOBS=2
# Native Cocoa style requires a Cocoa window; the unit runner uses Qt minimal.
export QT_STYLE_OVERRIDE=Fusion
deps=$PWD/local-deps
bash contrib/build-db4.sh "$deps"
bash ci/build-bls.sh "$deps"
./autogen.sh
./configure --with-gui=qt5 --enable-tests --disable-bench --disable-stacktraces \
    --disable-shared --with-boost="$p/opt/boost" \
    BDB_CFLAGS="-I$deps/include" BDB_LIBS="-L$deps/lib -ldb_cxx-4.8" \
    CPPFLAGS="-I$deps/include -I$p/opt/openssl@3/include -I$p/include" \
    LDFLAGS="-L$deps/lib -L$p/opt/openssl@3/lib -L$p/lib"
collect_logs() {
    result=$?
    trap - EXIT
    test ! -f config.log || cp config.log "$output/"
    for report in src/test/test_babacoin.{log,trs} src/qt/test/test_babacoin-qt.{log,trs}; do
        test ! -f "$report" || cp "$report" "$output/"
    done
    exit "$result"
}
trap collect_logs EXIT
make -j"$JOBS"
make check VERBOSE=1
python3 test/util/verify_test_reports.py
python3 test/util/isolated_smoke.py --daemon "$PWD/src/babacoind" --report "$output/smoke.json"
python3 test/lint/check-network-compatibility.py
mkdir -p "$output/bin"
cp src/babacoind src/babacoin-cli src/babacoin-tx src/qt/babacoin-qt "$output/bin/"
for binary in "$output"/bin/*; do otool -L "$binary" >> "$output/runtime-libraries.txt"; done
# The CLI binaries retain the documented Homebrew runtime dependencies. Bundle
# the GUI's libraries separately; no Apple signing/notarization is claimed.
app="$output/Babacoin-Qt.app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp src/qt/babacoin-qt "$app/Contents/MacOS/Babacoin-Qt"
cp share/qt/Info.plist "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleIconFile babacoin.icns" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :LSArchitecturePriority:0 $(uname -m)" "$app/Contents/Info.plist"
minimum_os=$(xcrun vtool -show-build "$app/Contents/MacOS/Babacoin-Qt" | awk '$1 == "minos" {print $2}')
[[ "$minimum_os" =~ ^[0-9]+\.[0-9]+(\.[0-9]+)?$ ]]
/usr/libexec/PlistBuddy -c "Set :LSMinimumSystemVersion $minimum_os" "$app/Contents/Info.plist"
cp src/qt/res/icons/babacoin.icns "$app/Contents/Resources/"
# Keep an explicitly deployed minimal plugin for the isolated headless app
# startup check; the normal desktop still selects its bundled Cocoa plugin.
mkdir -p "$app/Contents/PlugIns/platforms"
cp "$(brew --prefix qt@5)/plugins/platforms/libqminimal.dylib" "$app/Contents/PlugIns/platforms/"
macdeployqt "$app" -always-overwrite -verbose=2 -executable="$app/Contents/PlugIns/platforms/libqminimal.dylib"
codesign --force --deep --sign - "$app"
python3 ci/macos-bundle-smoke.py --app "$app" --report "$output/bundled-app-smoke.json"
sw_vers > "$output/build-environment.txt"
uname -a >> "$output/build-environment.txt"
clang++ --version >> "$output/build-environment.txt"
brew list --versions > "$output/packages.txt"
printf '%s\n' "$GITHUB_SHA" > "$output/SOURCE_COMMIT"
(cd "$output/bin" && sha256sum * > ../SHA256SUMS)
archive="babacoin-macos-$(uname -m)-test.tar.gz"
cp contrib/desktop/test-wallet.sh "$output/TEST-WALLET.sh"
cp doc/release/TEST-WALLET-README.txt "$output/TEST-WALLET-README.txt"
cp COPYING "$output/COPYING"
cp doc/release/dependency-notices.md "$output/DEPENDENCIES.md"
chmod +x "$output/TEST-WALLET.sh"
tar -czf "$output/$archive" -C "$output" bin Babacoin-Qt.app TEST-WALLET.sh TEST-WALLET-README.txt SOURCE_COMMIT COPYING DEPENDENCIES.md runtime-libraries.txt packages.txt
(cd "$output" && sha256sum "$archive" > "$archive.sha256")
