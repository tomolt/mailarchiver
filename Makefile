.POSIX:

include config.mk

SRC = mailarchiver.c
OBJ = ${SRC:.c=.o}

.PHONY: all clean install uninstall

all: mailarchiver

clean:
	rm -f $(OBJ) mailarchiver

install: mailarchiver
	# executable
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	cp -f mailarchiver "$(DESTDIR)$(PREFIX)/bin"
	chmod 755 "$(DESTDIR)$(PREFIX)/bin/mailarchiver"
	# man page
	mkdir -p "$(DESTDIR)$(MANPREFIX)/man1"
	cp -f mailarchiver.1 "$(DESTDIR)$(MANPREFIX)/man1"
	chmod 644 "$(DESTDIR)$(MANPREFIX)/man1/mailarchiver.1"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/mailarchiver"
	rm -f "$(DESTDIR)$(MANPREFIX)/man1/mailarchiver.1"

mailarchiver: $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $(OBJ)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

config.h:
	cp config.def.h $@

$(OBJ): config.h config.mk

