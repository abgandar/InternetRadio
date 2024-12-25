# InternetRadio
Raspberry Pi based internet radio player. Combining a cheap Raspberry (e.g. RPi Zero) with a USB soundcard (and WiFi dongle) allows you to hook it up to the AUX input of an old stereo system to enable it to play internet radio stations as well as your digital music library if you upload it to the RPi.

The UI is a simple to use web interface for both desktop and mobile browsers. It can either run on a simple local built-in HTTP server, or as a CGI script in a separate HTTP server.

This module relies on the [MusicPD](https://www.musicpd.org/) daemon and related libraries.
Fortunately, newer versions of Debian (bullseye upward) have mostly useable versions of mpd.

## Screenshots
![Desktop screenshot](https://github.com/abgandar/InternetRadio/blob/master/doc/desktop.png?raw=true)
![Mobile screenshot](https://github.com/abgandar/InternetRadio/blob/master/doc/mobile.png?raw=true)

## Installation
There are two ways to run InternetRadio: as a standalone HTTP server or as a CGI script from another server.

### Build and install (common for both)
```
sudo apt-get install mpd libmpdclient-dev git
git clone "https://github.com/abgandar/InternetRadio.git"

# build and install InternetRadio
cd InternetRadio/src
make
sudo make install
```

After this, you also need to configure MPD to work with your particular system (especially the sound card). A sample configuration file for my setup is provided in `examples`:
```
cp examples/mpd.conf /etc
```
Things you may want to change:
* audio_output: this is the sound card (via ALSA). You may need to tweak the device string ("plughw:CARD=S3,DEV=0") based on your system. Try `aplay -l` to list known ALSA devices.
* password: this is not really a safety feature as it is sent in plain text on the wire and in the HTML page. If you change it, you must also change it in `ir.html`.

### CGI script with LigHTTPd
```
apt-get install lighttpd
lighttpd-enable-mod 10-cgi.conf
cp examples/lighttpd.conf /etc/lighttpd
service enable lighttpd
```

If you have other services in LigHTTPd you may need to merge the relevant lines in the config file in `examples/lighttpd.conf`:
```
index-file.names            = ( "ir.html" )
static-file.exclude-extensions = ( ".cgi" )
server.breakagelog          = "/var/log/lighttpd/breakage.log"
```
These server the `ir.html` file as the index by default; prevent the `ir.cgi` executable file from being served as a static file; and record any error output from `ir.cgi` in `/var/log/lighttpd/breakage.log`.

### Standalone server
```
service enable ir
```
The executable is called `ir` (installed in `/usr/local/bin`), and a simple service script is provided so it should integrate with systemd as a system service.

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

### Power saving configuration
Streaming music is not terribly taxing even for small systems (eg RPi Zero). To save power, you can use some of the settings from `examples/config.txt` to reduce the frequencies of the RPi and to disable HDMI output. Place those in `/boot/config.txt`. You can also set the CPU governor to power saving mode at startup by copying `examples/rc.local` to `/etc/rc.local`.