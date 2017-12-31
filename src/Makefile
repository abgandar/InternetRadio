LDLIBS += -lmpdclient -lsystemd
CPPFLAGS += -Wall
DATESTAMP := `date +"%d%m%Y"`
GITREV := `git rev-parse HEAD`

all: ir.cgi ir.html

ir.cgi: ir-cgi.c config.h
	$(CC) -o $@ $< $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

ir.html: ir-raw.html
	sed -e "s/%DATE%/$(DATESTAMP)/g" -e "s/%GITREV%/$(GITREV)/g" ir-raw.html > ir.html

install: ir.cgi ir.html
	cp ir.cgi /usr/lib/cgi-bin
	setcap CAP_SYS_BOOT+ep /usr/lib/cgi-bin/ir.cgi
	cp ir.html radio.ico radio-0-75x.png radio-1x.png radio-2-6x.png radio-2x.png radio-4x.png radio-5-3x.png /var/www/html
	cp ../*.m3u /var/lib/mpd/playlists

uninstall:
	@rm /usr/lib/cgi-bin/ir.cgi
	@rm /var/www/html/ir.html /var/www/html/radio.ico /var/www/html/radio-0-75x.png /var/www/html/radio-1x.png /var/www/html/radio-2-6x.png /var/www/html/radio-2x.png /var/www/html/radio-4x.png /var/www/html/radio-5-3x.png
	@rm "/var/lib/mpd/playlists/Radio Auswahl.m3u" "/var/lib/mpd/playlists/Radio International.m3u" "/var/lib/mpd/playlists/Radio Deutschland.m3u"

clean:
	rm -f ir-cgi.o ir.cgi ir.html
