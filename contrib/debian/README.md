
Debian
====================
This directory contains files used to package babacoind/babacoin-qt
for Debian-based Linux systems. If you compile babacoind/babacoin-qt yourself, there are some useful files here.

## babacoin: URI support ##


babacoin-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install babacoin-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your babacoin-qt binary to `/usr/bin`
and the `../../share/pixmaps/babacoin128.png` to `/usr/share/pixmaps`

babacoin-qt.protocol (KDE)

