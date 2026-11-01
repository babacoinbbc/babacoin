#!/usr/bin/env bash
# Run INSIDE a disposable Ubuntu/Debian container, with /src mounted read-only.
# Never run this dependency installer directly on a production node.
set -euo pipefail
test "${BBC_DISPOSABLE_BUILD:-0}" = 1 || { echo 'Run inside a disposable container; see doc/ubuntu-compatibility.md' >&2; exit 2; }
test "$#" -eq 1 && test -d "$1"
output=$(cd "$1" && pwd)
export DEBIAN_FRONTEND=noninteractive
export JOBS=${JOBS:-2}
apt-get update
bsd_tools=bsdextrautils
if ! apt-cache show "$bsd_tools" >/dev/null 2>&1; then bsd_tools=bsdmainutils; fi
apt-get install -y --no-install-recommends ca-certificates curl git build-essential \
    libtool autotools-dev automake pkg-config libssl-dev libevent-dev libboost-all-dev \
    libminiupnpc-dev libzmq3-dev python3 cmake libgmp-dev patch "$bsd_tools" \
    qtbase5-dev qttools5-dev qttools5-dev-tools libqt5svg5-dev \
    libprotobuf-dev protobuf-compiler libqrencode-dev
dpkg-query -W -f='${Package}\t${Version}\n' > "$output/packages.tsv"
printf '%s\n' "${BBC_SOURCE_COMMIT:-local-uncommitted-source}" > "$output/SOURCE_COMMIT"
cat /etc/os-release > "$output/build-environment.txt"
uname -a >> "$output/build-environment.txt"
g++ --version >> "$output/build-environment.txt"
mkdir /build
tar -C /src --exclude=.git --exclude=depends/work --exclude=depends/built -cf - . | tar -C /build -xf -
cd /build
collect_logs() {
    result=$?
    trap - EXIT
    test ! -f config.log || cp config.log "$output/config.log"
    find /build -name test-suite.log -type f -exec cp --parents {} "$output/" \; || true
    for report in src/test/test_babacoin.{log,trs} src/qt/test/test_babacoin-qt.{log,trs}; do
        test ! -f "$report" || cp --parents "$report" "$output/"
    done
    exit "$result"
}
trap collect_logs EXIT
python3 test/lint/check-network-compatibility.py
python3 test/util/wallet_backup_safety_test.py
python3 test/util/test_report_safety_test.py
python3 test/util/release_archive_safety_test.py
python3 test/util/dkg_sender_safety_test.py
python3 test/util/bdb_configure_safety_test.py
# Bionic ships CMake 3.10, but the existing BLS 1.1.0 requires >=3.14.
# Extract a hash-checked official Python-distribution wheel without pip/root
# package upgrades. Its manylinux2014 baseline also works on Ubuntu 18.04.
if ! python3 -c 'import subprocess; v=subprocess.check_output(["cmake","--version"]).decode().split()[2]; assert tuple(map(int,v.split(".")[:2])) >= (3,14)'; then
    case "$(uname -m)" in
        x86_64)
            wheel_url=https://files.pythonhosted.org/packages/51/e0/fb9182958e9c07366f991cf75fdc899a8d8bca6855d47d97728f5fdb15b2/cmake-3.22.6-py2.py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl
            wheel_sha=a2357c510e37db7df156c31ab43857b4583e3a2ece8220747e8a007aea50aa45
            ;;
        aarch64)
            wheel_url=https://files.pythonhosted.org/packages/0c/f7/ac0c487d1119a7c5119b02a0546e82385d96779125861d17c5e75d479603/cmake-3.22.6-py2.py3-none-manylinux_2_17_aarch64.manylinux2014_aarch64.whl
            wheel_sha=41837714e05f2c2211afa67f5c5fc05c06732e343ac78f81a929b6516e7588c4
            ;;
        *) echo 'Unsupported architecture for Bionic CMake bootstrap' >&2; exit 1 ;;
    esac
    curl -fL --retry 3 --proto '=https' --tlsv1.2 "$wheel_url" -o /build/cmake.whl
    echo "$wheel_sha  /build/cmake.whl" | sha256sum -c -
    python3 -m zipfile -e /build/cmake.whl /build/cmake-wheel
    export PATH="/build/cmake-wheel/cmake/data/bin:$PATH"
    chmod +x /build/cmake-wheel/cmake/data/bin/*
fi
bash contrib/build-db4.sh /build/local
bash ci/build-bls.sh /build/local
./autogen.sh
./configure --with-gui=qt5 --enable-tests --disable-bench --disable-stacktraces --disable-shared \
    BDB_CFLAGS=-I/build/local/include BDB_LIBS='-L/build/local/lib -ldb_cxx-4.8' \
    CPPFLAGS=-I/build/local/include LDFLAGS=-L/build/local/lib
make -j"$JOBS"
# Keep complete failing-test output and fail the job if any suite fails.
make check VERBOSE=1
python3 test/util/verify_test_reports.py
python3 test/util/isolated_smoke.py --daemon /build/src/babacoind --report "$output/smoke.json"
mkdir -p "$output/bin"
cp src/babacoind src/babacoin-cli src/babacoin-tx src/qt/babacoin-qt "$output/bin/"
for bin in "$output"/bin/*; do
    ldd "$bin" >> "$output/runtime-libraries.txt"
done
(cd "$output/bin" && sha256sum * > ../SHA256SUMS)
. /etc/os-release
cp contrib/desktop/test-wallet.sh "$output/bin/TEST-WALLET.sh"
cp doc/release/TEST-WALLET-README.txt "$output/bin/TEST-WALLET-README.txt"
cp "$output/SOURCE_COMMIT" "$output/bin/SOURCE_COMMIT"
cp "$output/runtime-libraries.txt" "$output/bin/RUNTIME-LIBRARIES.txt"
cp "$output/packages.tsv" "$output/bin/PACKAGES.tsv"
cp COPYING "$output/bin/COPYING"
cp doc/release/dependency-notices.md "$output/bin/DEPENDENCIES.md"
chmod +x "$output/bin/TEST-WALLET.sh"
archive="babacoin-$ID-$VERSION_ID-$(uname -m).tar.gz"
tar -czf "$output/$archive" -C "$output/bin" .
(cd "$output" && sha256sum "$archive" > "$archive.sha256")
cp config.log "$output/config.log"
echo 'Build, unit/GUI tests and isolated wallet smoke test passed.'
