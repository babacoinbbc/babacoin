Babacoin Core
==========

This is the official reference wallet for Babacoin digital currency and comprises the backbone of the Babacoin peer-to-peer network. You can [download Babacoin Core](https://www.babacoin.network/downloads/) or [build it yourself](#building) using the guides below.

Running
---------------------
The following are some helpful notes on how to run Babacoin on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/babacoin-qt` (GUI) or
- `bin/babacoind` (headless)

### Windows

Unpack the files into a directory, and then run babacoin-qt.exe.

### OS X

Drag Babacoin-Qt to your applications folder, and then run Babacoin-Qt.

### Need Help?

* See the [Babacoin documentation](https://docs.babacoin.network)
for help and more information.
* See the [Babacoin Developer Documentation](https://babacoin-docs.github.io/) 
for technical specifications and implementation details.
* Ask for help on [Babacoin Nation Discord](http://babacoinchat.org)
* Ask for help on the [Babacoin Forum](https://babacoin.network/forum)

Building
---------------------
The following are developer notes on how to build Babacoin Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [OS X Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [Gitian Building Guide](gitian-building.md)

Development
---------------------
The Babacoin Core repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- Source Code Documentation ***TODO***
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [Travis CI](travis-ci.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)

### Resources
* Discuss on the [Babacoin Forum](https://babacoin.network/forum), in the Development & Technical Discussion board.
* Discuss on [Babacoin Nation Discord](http://babacoinchat.org)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
