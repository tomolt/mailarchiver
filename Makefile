include config.mk

.POSIX:

.PHONY: all clean install uninstall

all: mailarchiver

clean:
	rm -f mailarchiver.o mailarchiver

install: mailarchiver
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	cp -f mailarchiver "$(DESTDIR)$(PREFIX)/bin"
	chmod 644 "$(DESTDIR)$(PREFIX)/bin/mailarchiver"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/mailarchiver"

mailarchiver: mailarchiver.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

mailarchiver.o: mailarchiver.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

