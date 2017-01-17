all: ir.cgi

ir.cgi: ir-cgi.c config.h
	$(CC) -o $@ $< $(CPPFLAGS)

install: ir.cgi ir.html
	cp ir.cgi /usr/lib/cgi-bin
	cp ir.html /var/www/html

uninstall:
	@rm /usr/lib/cgi-bin/ir.cgi
	@rm /var/www/html/ir.html

clean:
	rm -f ir-cgi.o ir.cgi
