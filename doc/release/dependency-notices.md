# Dependency source and license notices

Babacoin's own license is in `COPYING`. Dependencies retain their respective
licenses; they are not relicensed by this project. Build-package and runtime
library lists accompany each release platform. Windows packages additionally
include the dependency license files supplied by MSYS2.

Rebuild the exact pinned dependencies with the scripts in the tagged source:

| Dependency | Source / license information |
|---|---|
| Berkeley DB 4.8.30 | `contrib/build-db4.sh`; [upstream source and license](https://download.oracle.com/berkeley-db/db-4.8.30.NC.tar.gz) |
| BLS wrapper | [dashpay/bls-signatures at 2daa8f8](https://github.com/dashpay/bls-signatures/tree/2daa8f8e220d9bd6aa0ac42a2eb550bb8c7341b5), built by `ci/build-bls.sh` |
| Relic | Dependency revision and license selected by the pinned BLS source above |
| libsodium | [Source and license](https://github.com/jedisct1/libsodium) |
| GMP | [Source and license](https://gmplib.org/) |
| Qt 5 | [Qt source and licensing](https://www.qt.io/licensing/); dynamically linked GUI libraries |
| OpenSSL | [Source and license](https://github.com/openssl/openssl) |
| Boost | [Source and license](https://www.boost.org/users/license.html) |
| libevent | [Source and license](https://github.com/libevent/libevent) |
| ZeroMQ | [Source and license](https://github.com/zeromq/libzmq) |
| Protobuf / Abseil | [Protobuf](https://github.com/protocolbuffers/protobuf), [Abseil](https://github.com/abseil/abseil-cpp) |
| MiniUPnPc | [Source and license](https://github.com/miniupnp/miniupnp) |
| libqrencode | [Source and license](https://github.com/fukuchi/libqrencode) |
| secp256k1 / UniValue / LevelDB | Vendored source and notices under `src/` |

Platform dependencies and toolchains can add further runtime libraries. Consult
their packaged notices and the exact package-version lists, not this overview
alone. Ad hoc application signing is not a publisher identity certificate.
