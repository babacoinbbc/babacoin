Babacoin Core
==========

Intro
-----
Babacoin is a free open source peer-to-peer electronic cash system that is
completely decentralized, without the need for a central server or trusted
parties.  Users hold the crypto keys to their own money and transact directly
with each other, with the help of a P2P network to check for double-spending.


Setup
-----
Unpack the files into a directory and run babacoin-qt.exe.

Babacoin Core is the original Babacoin client and it builds the backbone of the network.
However, it downloads and stores the entire history of Babacoin transactions;
depending on the speed of your computer and network connection, the synchronization
process can take anywhere from a few hours to a day or more.

See the Babacoin documentation at:
  https://docs.babacoin.network
for more help and information.

Moved or disconnected data drive (next maintenance update)
--------------------------------------------------------
If a saved data folder or a shortcut's -datadir path is unavailable, the
desktop wallet asks you to locate the existing Babacoin data folder instead
of exiting. Reconnect the drive and click "Check again", or browse to its new
location and click "Use this folder". Select the data folder, not wallet.dat.

Recovery does not move, delete, repair or copy files. It refuses missing or
empty folders rather than creating a new empty wallet. The normal default
wallet must be present as wallet.dat (inside wallets/ when that directory
exists). Explicit relative -wallet selections are also recognized. Custom
wallet names configured only in babacoin.conf may require updating the
shortcut's -datadir path directly before startup.

The selected location is remembered in this Windows user's desktop settings,
including a mapping for an old shortcut. The shortcut itself is not rewritten.
If its original drive returns, the wallet asks which existing location to use;
it does not silently switch back. Cancel leaves the selection unchanged.
The command-line daemon keeps its existing strict -datadir behavior.

This recovery feature is not included in the original v2.0.1 download.
