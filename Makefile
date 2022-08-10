include config.mk

SRC = mailarchiver.c
OBJ = ${SRC:.c=.o}

.POSIX:

.PHONY: all clean install uninstall

all: mailarchiver

clean:
	rm -f $(OBJ) mailarchiver

install: mailarchiver
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	cp -f mailarchiver "$(DESTDIR)$(PREFIX)/bin"
	chmod 644 "$(DESTDIR)$(PREFIX)/bin/mailarchiver"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/mailarchiver"

mailarchiver: $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

config.h:
	cp config.def.h $@

$(OBJ): config.h config.mk

