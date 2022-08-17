.POSIX:

include config.mk

BIN = smak-readmsg smak-genindex
SRC = $(addsuffix .c,$(BIN)) encode.c mail.c util.c
OBJ = ${SRC:.c=.o}
MAN = $(addsuffix .1,$(BIN))

.PHONY: all clean install uninstall

all: $(BIN)

clean:
	rm -f $(OBJ) $(BIN)

install: $(BIN) $(MAN)
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	mkdir -p "$(DESTDIR)$(MANPREFIX)/man1"
	for b in $(BIN); do					\
		cp -f $b "$(DESTDIR)$(PREFIX)/bin"		\
		chmod 755 "$(DESTDIR)$(PREFIX)/bin/$b"		\
		cp -f $b.1 "$(DESTDIR)$(MANPREFIX)/man1"	\
		chmod 644 "$(DESTDIR)$(MANPREFIX)/man1/$b.1"	\
	done

uninstall:
	for b in $(BIN); do					\
		rm -f "$(DESTDIR)$(PREFIX)/bin/$b"		\
		rm -f "$(DESTDIR)$(MANPREFIX)/man1/$b.1"	\
	done

smak-readmsg: smak-readmsg.o encode.o mail.o util.o
	$(LD) $(LDFLAGS) -o $@ $^

smak-genindex: smak-genindex.o encode.o util.o
	$(LD) $(LDFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<

config.h:
	cp config.def.h $@

$(OBJ): config.mk

encode.o: encode.h util.h
mail.o: mail.h
util.o: util.h
smak-readmsg.o: arg.h config.h encode.h mail.h util.h
smak-genindex.o: config.h encode.h util.h

