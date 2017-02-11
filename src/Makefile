LDLIBS += -lmpdclient
CPPFLAGS += -Wall

all: ir.cgi

ir.cgi: ir-cgi.c config.h
	$(CC) -o $@ $< $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

install: ir.cgi ir.html
	cp ir.cgi /usr/lib/cgi-bin
	cp ir.html  /var/www/html
	cp ../*.m3u /var/lib/mpd/playlists

uninstall:
	@rm /usr/lib/cgi-bin/ir.cgi
	@rm /var/www/html/ir.html
	@rm "/var/lib/mpd/playlists/Radio Auswahl.m3u" "/var/lib/mpd/playlists/Radio International.m3u" "/var/lib/mpd/playlists/Radio Deutschland.m3u"

clean:
	rm -f ir-cgi.o ir.cgi
