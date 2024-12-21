# InternetRadio
Raspberry Pi based internet radio player. Combining a cheap Raspberry (e.g. RPi Zero) with a USB soundcard (and WiFi dongle) allows you to hook it up to the AUX input of an old stereo system to enable it to play internet radio stations as well as your digital music library if you upload it to the RPi.

This module relies on the [MusicPD](https://www.musicpd.org/) daemon and related libraries.

Newer versions of Debian have useable versions of mpd. Unfortunately the libmpdclient2 package is missing the headers required to compile the program, so this (small) library needs to be buit and installed from source.

## Installation
There are two ways to run InternetRadio: as a standalone HTTP server or as a CGI script from another server.

### Build and install (common for both)
```
sudo apt-get install mpc git meson
git clone "https://github.com/abgandar/InternetRadio.git"
cd InternetRadio

# build & install libmpdclient
tar -xf libmpdclient-2.22
cd libmpdclient-2.22
meson setup output
ninja -C output
sudo ninja -C output install

# build and install InternetRadio
cd ../src
make
sudo make install
```

After this, you also need to configure MPD to work with your particular system (especially the sound card). A sample configuration file for my setup is provided in `examples`:
```
cp examples/mpd.conf /etc
```
Things you may want to change:
* audio_output: this is the sound card (via alsa), you may need to tweak the device string ("plughw:CARD=S3,DEV=0") based on your system
* password setting: this is not really secure as it is also be sent in plain text in the HTML source. If you change it, you must also change it in `ir.html`

### CGI script with LigHTTPd
```
apt-get install lighttpd
lighttpd-enable-mod 10-cgi.conf
cp examples/lighttpd.conf /etc/lighttpd
service enable lighttpd
```
If you have other services in LigHTTPd you need to merge the relevant lines in the config file in `examples/lighttpd.conf`.

### Standalone server
```
service enable ir
```
The executable is called `ir` (installed in `/usr/local/bin`), and a simple service script is provided so it should integrate with systemd.

## Optional

### Windows File Sharing
Files are stored in the default path of MPD: playlists (M3U) in `/var/lib/mpd/playlists/` and additional music files in `/var/lib/mpd/music`. InternetRadio comes with sample internet radio lists (which are not well maintained so some stations may not work). If you want access to these files via Windows file sharing (SMB), you can install `samba`:
```
apt-get install samba
cp examples/smb.conf /etc/samba
```
This will create two shares for music and playlists, just drop your files there and MPD should notice them.

### IPTables firewall
To protect your system a little from unwanted network traffic you can set up IPTables to filter out any traffic to ports other than the ones used by your setup.
```
apt-get install iptables-persistent
cp examples/rules.v4 /etc/iptables
```
