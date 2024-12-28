DATESTAMP := $(shell date +"%d%m%Y")
TIMESTAMP := $(shell date +"%d%m%y%H%M%S")
GITREV := $(shell git rev-parse HEAD)

LDLIBS += -lmpdclient
#LDLIBS += -lsystemd
CPPFLAGS += -Wall -O3 -s -ffunction-sections -fdata-sections -Wl,--gc-sections -DTIMESTAMP='"\"$(TIMESTAMP)\""'
#CPPFLAGS += -Wall -O1 -DTIMESTAMP='"\"$(TIMESTAMP)\""' -g

LDBINFLAGS += -z noexecstack -z relro -r -b binary

ifdef EASTEREGG
EASTEROBJ = easteregg-png.o
else
EASTEROBJ =
endif

all: ir.cgi ir ir.html

ir.cgi: ir-cgi.o ir-common.o
	$(CC) -o $@ $^ $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

radio-ico.o: radio.ico
	$(LD) $(LDBINFLAGS) $^ -o $@

radio-0-75x-png.o: radio-0-75x.png
	$(LD) $(LDBINFLAGS) $^ -o $@

radio-1x-png.o: radio-1x.png
	$(LD) $(LDBINFLAGS) $^ -o $@

radio-2-6x-png.o: radio-2-6x.png
	$(LD) $(LDBINFLAGS) $^ -o $@

radio-2x-png.o: radio-2x.png
	$(LD) $(LDBINFLAGS) $^ -o $@

radio-4x-png.o: radio-4x.png
	$(LD) $(LDBINFLAGS) $^ -o $@

radio-5-3x-png.o: radio-5-3x.png
	$(LD) $(LDBINFLAGS) $^ -o $@

easteregg-png.o: easteregg.png
	$(LD) $(LDBINFLAGS) $^ -o $@

ir-html.o: ir.html
	$(LD) $(LDBINFLAGS) $^ -o $@

ir: ir-html.o radio-ico.o radio-0-75x-png.o radio-1x-png.o radio-2x-png.o radio-2-6x-png.o radio-2x-png.o radio-4x-png.o radio-5-3x-png.o $(EASTEROBJ) ir-common.o ir.o http-server.a
	$(CC) -o $@ $^ $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

http-server.a: http-server.o
	ar r $@ $^
	ranlib $@

httpd: httpd.o http-server.a
	$(CC) -o $@ $^ $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)
	strip $@

ir.html: ir-raw.html
	sed -e "s/%DATE%/$(DATESTAMP)/g" -e "s/%GITREV%/$(GITREV)/g" ir-raw.html > ir.html

install: ir ir.cgi ir.html
	cp ir /usr/local/bin
	setcap CAP_SYS_BOOT+ep /usr/local/bin/ir
	cp ir.service /etc/systemd/system/
	cp ir.cgi /usr/lib/cgi-bin/
	setcap CAP_SYS_BOOT+ep /usr/lib/cgi-bin/ir.cgi
	cp ir.html radio.ico radio-0-75x.png radio-1x.png radio-2-6x.png radio-2x.png radio-4x.png radio-5-3x.png /var/www/html
	cp ../*.m3u /var/lib/mpd/playlists/
	@systemctl stop mpd
	@rm /var/lib/mpd/state
	@systemctl start mpd

uninstall:
	@rm /usr/lib/cgi-bin/ir.cgi
	@rm /usr/local/bin/ir
	@rm /etc/systemd/system/ir.service
	@rm /var/www/html/ir.html /var/www/html/radio.ico /var/www/html/radio-0-75x.png /var/www/html/radio-1x.png /var/www/html/radio-2-6x.png /var/www/html/radio-2x.png /var/www/html/radio-4x.png /var/www/html/radio-5-3x.png
	@rm "/var/lib/mpd/playlists/Radio Auswahl.m3u" "/var/lib/mpd/playlists/Radio International.m3u" "/var/lib/mpd/playlists/Radio Deutschland.m3u"

clean:
	rm -f *.o ir.cgi ir ir.html
