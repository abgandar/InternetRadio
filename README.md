# InternetRadio
Raspberry Pi based internet radio module for older stereo systems.

This module relies on the excellent [MusicPD](https://www.musicpd.org/) daemon and related libraries.
Before building this module, you need to install a current libmpclient in your system. Unfortunately, debian packages are
horribly out of date. So your best bet is to build from scratch. The same goes for the mpd server, which is required for
running the internet radio module.
