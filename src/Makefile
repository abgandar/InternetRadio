LDLIBS += -lmpdclient
#LDLIBS += -lsystemd
#CPPFLAGS += -Wall -O2 -s -ffunction-sections -fdata-sections -Wl,--gc-sections
CPPFLAGS += -Wall -O2
DATESTAMP := `date +"%d%m%Y"`
GITREV := `git rev-parse HEAD`

ifdef EASTEREGG
EASTEROBJ = easteregg-png.o
else
EASTEROBJ = ""
endif

all: ir.cgi ir ir.html

ir.cgi: ir-cgi.o ir-common.o
	$(CC) -o $@ $^ $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

radio-ico.o: radio.ico
	ld -r -b binary $^ -o $@

radio-0-75x-png.o: radio-0-75x.png
	ld -r -b binary $^ -o $@

radio-1x-png.o: radio-1x.png
	ld -r -b binary $^ -o $@

radio-2-6x-png.o: radio-2-6x.png
	ld -r -b binary $^ -o $@

radio-2x-png.o: radio-2x.png
	ld -r -b binary $^ -o $@

radio-4x-png.o: radio-4x.png
	ld -r -b binary $^ -o $@

radio-5-3x-png.o: radio-5-3x.png
	ld -r -b binary $^ -o $@

easteregg-png.o: easteregg.png
	ld -r -b binary $^ -o $@

ir-html.o: ir.html
	ld -r -b binary $^ -o $@

ir: ir-html.o radio-ico.o radio-0-75x-png.o radio-1x-png.o radio-2x-png.o radio-2-6x-png.o radio-2x-png.o radio-4x-png.o radio-5-3x-png.o $(EASTEROBJ) ir-server.o ir-common.o
	$(CC) -o $@ $^ $(CPPFLAGS) $(LDFLAGS) $(LDLIBS)

ir.html: ir-raw.html
	sed -e "s/%DATE%/$(DATESTAMP)/g" -e "s/%GITREV%/$(GITREV)/g" ir-raw.html > ir.html

install: ir ir.cgi ir.html
	cp ir /usr/local/bin
	setcap CAP_SYS_BOOT+ep /usr/local/bin/ir
	cp ir.service /etc/systemd/system/
	cp ir.cgi /usr/lib/cgi-bin/
	setcap CAP_SYS_BOOT+ep /usr/lib/cgi-bin/ir.cgi
#	cp ir.html radio.ico radio-0-75x.png radio-1x.png radio-2-6x.png radio-2x.png radio-4x.png radio-5-3x.png /var/www/html
	cp ../*.m3u /var/lib/mpd/playlists/

uninstall:
	@rm /usr/lib/cgi-bin/ir.cgi
	@rm /usr/local/bin/ir
	@rm /etc/systemd/system/ir.service
	@rm /var/www/html/ir.html /var/www/html/radio.ico /var/www/html/radio-0-75x.png /var/www/html/radio-1x.png /var/www/html/radio-2-6x.png /var/www/html/radio-2x.png /var/www/html/radio-4x.png /var/www/html/radio-5-3x.png
	@rm "/var/lib/mpd/playlists/Radio Auswahl.m3u" "/var/lib/mpd/playlists/Radio International.m3u" "/var/lib/mpd/playlists/Radio Deutschland.m3u"

clean:
	rm -f *.o ir.cgi ir ir.html
