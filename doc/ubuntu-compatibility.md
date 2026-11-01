# Ubuntu compatibility validation

This work targets Ubuntu **18.04, 20.04, 22.04, 24.04 and 26.04 LTS** on
x86_64 and ARM64. A target in the matrix is not a certification: check the
actual test results for the exact commit and architecture. Interim `.10`
releases and other architectures are not covered by this matrix.

Ubuntu 18.04/20.04 also need appropriate vendor security maintenance. Building
Babacoin on an old OS does not supply missing OS security updates.

## Reproduce a build on a disposable Docker host

Run from the repository root, on a native x86_64 or ARM64 Docker host:

```sh
mkdir -p /tmp/bbc-ubuntu24-output
docker run --rm --init -e JOBS=2 -e BBC_DISPOSABLE_BUILD=1 \
  --mount "type=bind,source=$PWD,target=/src,readonly" \
  --mount type=bind,source=/tmp/bbc-ubuntu24-output,target=/output \
  ubuntu:24.04 bash /src/ci/ubuntu-compat.sh /output
```

Replace the image tag with another LTS in the matrix and use a fresh output
directory. This installs dependencies only inside the container. It builds the
daemon, CLI, transaction utility and Qt GUI, runs `make check`, and runs an
isolated wallet/daemon smoke test. Failures stop the job. Native unit/GUI tests
are not silently disabled. Test and configure logs are retained on failure.

On Windows, use `contrib/prepare-source.py --output C:/path/outside/repo/source.tar`
to transport an edited checkout to Linux with its executable bits and LF line
endings. A Windows filesystem tar copy can lose both.

## Wallet database compatibility

The tested build path uses **Berkeley DB 4.8.30**, with the existing compiler
compatibility patch and a verified source archive. It does not silently choose
Ubuntu's BDB 5.3. The default configure check also applies when `BDB_CFLAGS`
was supplied manually. `--with-incompatible-bdb` is an explicit opt-in for
non-portable builds, not an upgrade recommendation.

For a manual build, install the dependencies listed in `ci/ubuntu-compat.sh`,
provide CMake >=3.14, and then:

```sh
bash contrib/build-db4.sh "$PWD/local-deps"
bash ci/build-bls.sh "$PWD/local-deps"
./autogen.sh
./configure --with-gui=qt5 --enable-tests --disable-bench --disable-stacktraces \
  BDB_CFLAGS="-I$PWD/local-deps/include" \
  BDB_LIBS="-L$PWD/local-deps/lib -ldb_cxx-4.8" \
  CPPFLAGS="-I$PWD/local-deps/include" LDFLAGS="-L$PWD/local-deps/lib"
make -j2
make check VERBOSE=1
python3 test/util/verify_test_reports.py
```

The CMake bootstrap in the container handles Ubuntu 18.04's older CMake. BLS
remains the existing 1.1.0 implementation, pinned to commit
`2daa8f8e220d9bd6aa0ac42a2eb550bb8c7341b5`; its Relic dependency is also pinned
by that upstream revision. No signature encoding or consensus algorithm is
replaced by a newer cryptography library.

A wallet already opened by BDB 5.3 is not guaranteed to open with BDB 4.8.
Do not overwrite a live wallet or try an automatic downgrade. Take a successful
RPC backup with the original working binary, keep that binary and backup, shut
it down cleanly, and validate only an additional copy in an isolated directory.
Never accept an installer message as evidence that a backup can be restored.

## Isolated and cross-version tests

```sh
python3 test/util/isolated_smoke.py --daemon /absolute/path/babacoind \
  --report /tmp/bbc-smoke.json
python3 test/util/cross_version_smoke.py \
  --old-daemon /absolute/path/old/babacoind \
  --new-daemon /absolute/path/new/babacoind --report /tmp/bbc-cross.json
# Repeat using an encrypted disposable wallet:
python3 test/util/cross_version_smoke.py --encrypted \
  --old-daemon /absolute/path/old/babacoind \
  --new-daemon /absolute/path/new/babacoind --report /tmp/bbc-cross-encrypted.json
```

These commands create new temporary wallets. They never open the user's normal
data directory. The cross-version test exercises wallet reopen/backup/restore
and permits P2P connections only through explicitly bound localhost ports.
It does not demonstrate historical chain replay, spending real balances,
mainnet mining, smartnode operation, or agreement across consensus activation heights.
The isolated regtest smoke test mines one test block, verifies wallet credit, and
replays that block after restoring a backup; these are not real BBC funds.

The v1.0.0 binary has broken regtest genesis assertions. For an offline baseline
wallet test of that binary, pass `--wallet-network main` to `isolated_smoke.py`.
This still disables external peer connections and uses a new empty directory.

## Network preservation and release gate

`python3 test/lint/check-network-compatibility.py` checks 284 protected source
files against `main@7cdd64b4435d5fef4f63accfaa1706f2425a3324`. Mainnet parameters,
proof of work, transaction validation, historical founder/collateral schedules,
protocol minimums and the existing spork configuration remain at that baseline.
The BLS wrapper header records the approved initialization/copy/move portability
fix separately from its original hash. `src/chain.h` also records an
approved fix: the three casts introduced after v1.0.0 discarded height/file
values during disk decoding. Removing them restores the exact v1.0.0 header
after newline normalization; the serialized database bytes are unchanged.
Nonzero height/file round-trip tests and restart tests guard this regression.
The DKG message handler also records a peer-attribution fix: a mixed-sender
invalid aggregate must fall back to individual signature checks instead of
blaming the last sender. Seven controlled-signature scenarios execute the
production function; this test does not replace real BLS or smartnode testing.
The utility module records the removal of two diagnostic stdout lines from
`GetChainName`. Network selection returns the same value; CLI JSON/hex output
is machine-readable again. The 49 transaction-utility scenarios validate this
alongside BBC address/WIF fixture versions. Its configuration reader uses the
Python 3.6-compatible `read_file` API, including on newer Ubuntu Python versions.
The missing `share/rpcauth/rpcauth.py` helper is restored from MIT-licensed
Bitcoin Core v0.21.0, retaining the salt/password/HMAC API covered by the three
existing tests. Only the printed configuration filename is adapted.
The vendored secp256k1 test requires cross-library parsing for valid scalar
pairs: OpenSSL 3 rejects negative DER integers immediately, while secp256k1
parses their encoding into invalid zero scalars. A deterministic negative
scalar vector covers this difference; production crypto code is unchanged.

The regtest constructor is excluded so its incorrect parameters can be
corrected independently of mainnet rules. The broken regtest genesis is
replaced by a valid easy-PoW genesis, and its intended no-retargeting setting
is respected at all test heights. Existing regtest data is not compatible:
use a new test directory; never delete or migrate a production wallet. Its
founder address is also encoded with the regtest prefix while retaining the
same destination key hash, so test block generation can decode the recipient.
Its spork destination uses the matching regtest prefix, and its quorum map
keys now agree with the configured quorum types.
Regtest defines 10 BBC collateral and initializes every quorum type that its
dynamic quorum update can activate. This makes isolated smartnode tests
fundable with the existing initial 4 BBC subsidy and prevents missing-cache
construction. Production collateral tiers and reward percentages are unchanged.

The core test shutdown stubs now return failure on fatal shutdown requests.
`verify_test_reports.py` additionally requires Boost's completion marker and
all six Qt suite completion markers, rejecting a premature successful exit.

For a bounded historical replay, extract only verified raw `blkNNNNN.dat`
files into a separate reference directory, then run:

```sh
python3 test/util/offline_chain_replay.py --raw-blocks /absolute/raw-blocks \
  --old-daemon /absolute/old/babacoind --new-daemon /absolute/new/babacoind \
  --max-height 1000 --report /tmp/bbc-chain-sample.json
```

Both nodes reindex independent copies with external P2P disabled and
`assumevalid=0`; their tips and UTXO summaries must match. The report labels
bounded samples explicitly. Omitting `--max-height` requests the whole archive
and can take days on this hardware. Archive tip/header scanning alone does
not validate proof of work, transactions or current network consensus.

This is a source-change guard, not a formal safety proof. The baseline already
differs from v1.0.0: the 1,250,000 payment/collateral activation, immediate spork
key rotation, and founder validation changes require separate network review.
Do not claim all historical versions agree just because the OS builds pass.

The release workflow requires the entire Ubuntu build/test matrix to succeed
and assembles a **draft** release. The former "missing platforms" workflow no
longer overwrites an existing release. ARM64 Ubuntu artifacts replace the old
separate Raspberry Pi build path; actual Raspberry Pi hardware still requires
its own runtime check. macOS/Windows jobs need their own platform validation.

Primary references: [Ubuntu lifecycle](https://ubuntu.com/about/release-cycle),
[GitHub runner availability](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
[BLS 1.1.0 source](https://github.com/dashpay/bls-signatures/tree/2daa8f8e220d9bd6aa0ac42a2eb550bb8c7341b5).
