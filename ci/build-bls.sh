#!/usr/bin/env bash
# Same BLS release as the existing workflows; pin its immutable commit.
set -euo pipefail
if [ "$#" -ne 1 ] || [[ "$1" != /* ]] || [[ "$1" =~ [[:space:]] ]]; then
    echo "Usage: bash ci/build-bls.sh /absolute/prefix-without-spaces" >&2
    exit 2
fi
prefix=$1
build=$(mktemp -d "${TMPDIR:-/tmp}/babacoin-bls.XXXXXXXX")
trap 'rm -rf -- "$build"' EXIT
git init "$build/source"
git -C "$build/source" remote add origin https://github.com/dashpay/bls-signatures.git
git -C "$build/source" fetch --depth 1 origin 2daa8f8e220d9bd6aa0ac42a2eb550bb8c7341b5
git -C "$build/source" checkout --detach FETCH_HEAD
test "$(git -C "$build/source" rev-parse HEAD)" = 2daa8f8e220d9bd6aa0ac42a2eb550bb8c7341b5
# Prevent an inherited developer override from selecting a moving Relic branch.
unset RELIC_MAIN
for file in "$build/source/src/threshold.cpp" "$build/source/src/threshold.hpp"; do
    sed -i '1i#include <memory>' "$file"
done
mkdir "$build/output"
cd "$build/output"
cmake ../source -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC \
    -DBUILD_BLS_PYTHON_BINDINGS=0 -DBUILD_BLS_TESTS=0 -DBUILD_BLS_BENCHMARKS=0
make -j"${JOBS:-2}"
# Upstream's install target preserves nested Relic include paths.
test -s libbls-dash.a
make install
test -s "$prefix/lib/libbls-dash.a"
test -s "$prefix/include/bls-dash/bls.hpp"
