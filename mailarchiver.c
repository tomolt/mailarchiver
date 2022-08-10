#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>

#include "config.h"

struct mail {
	char *subject;
	char *author;
	char *date;
	char *content;
};

struct mail mail;

void
die(const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

/* only uppercase allowed! */
static inline bool
is_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

static inline bool
is_ws(char c)
{
	/* TODO */
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool
is_key(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool
parse_header(char **pointer, bool (*field_cb)(char *key, char *value))
{
	char *cursor = *pointer;
	char *key, *value;

	for (;;) {
		if (!*cursor) return false;
		if (*cursor == '\n') break;

		if (!is_key(*cursor)) return false;
		key = cursor;
		do cursor++; while (is_key(*cursor));
		if (*cursor != ':') return false;
		*cursor++ = '\0';

		value = cursor;
		do {
			while (*cursor != '\n') {
				if (!*cursor) return false;
				cursor++;
			}
			cursor++;
		} while (is_ws(*cursor) && *cursor != '\n');
		*(cursor-1) = '\0';

		if (!field_cb(key, value)) return false;
	}

	*pointer = cursor;
	return true;
}

void
normalize_ws(char *str)
{
	char *rhead, *whead;
	bool inws;

	rhead = whead = str;
	inws = true;
	while (*rhead) {
		if (is_ws(*rhead)) {
			if (!inws) inws = true, *whead++ = ' ';
		} else {
			inws = false, *whead++ = *rhead;
		}
		rhead++;
	}
	if (inws) --whead;
	*whead = '\0';
}

bool
decode_qprintable(char *str)
{
	char *rhead, *whead, *eq;

	rhead = whead = str;
	while ((eq = strchr(rhead, '='))) {
		memmove(whead, rhead, eq - rhead);
		whead += eq - rhead;
		rhead = eq + 1;
		if (is_hex(rhead[0]) && is_hex(rhead[1])) {
			*whead++ = (rhead[0] >= 'A' ? rhead[0] - 'A' + 10 : rhead[0] - '0') * 16 +
				(rhead[1] >= 'A' ? rhead[1] - 'A' + 10 : rhead[1] - '0');
			rhead += 2;
		} else if (rhead[0] == '\r' && rhead[1] == '\n') {
			rhead += 2;
		} else if (rhead[0] == '\n') {
			rhead += 1;
		} else return false;
	}
	memmove(whead, rhead, strlen(rhead));
	whead += strlen(rhead);
	*whead = '\0';
	return true;
}

static int
decode_base64_digit(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

bool
decode_base64(char *str)
{
	/* This implementation is terribly inefficient, but it should suffice for now. */

	char *rhead, *whead;
	unsigned long value;
	int digit;
	int bits;

	rhead = whead = str;
	value = 0;
	bits = 0;
	while (*rhead && *rhead != '=') {
		digit = decode_base64_digit(*rhead++);
		if (digit < 0) return false;
		value <<= 6;
		value |= digit;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			*whead++ = value >> bits;
			value &= (1u << bits) - 1u;
		}
	}
	*whead = '\0';
	return true;
}

bool
parse_field(char *key, char *value)
{
	if (!strcasecmp(key, "From")) {
		normalize_ws(value);
		mail.author = value;
	} else if (!strcasecmp(key, "Subject")) {
		normalize_ws(value);
		mail.subject = value;
	} else if (!strcasecmp(key, "Date")) {
		normalize_ws(value);
		mail.date = value;
#if 0
	} else if (!strcasecmp(key, "Content-Transfer-Encoding")) {
		if (!strcasecmp(key, "quoted-printable")) {
		} else if (!strcasecmp(key, "base64")) {
		} else {
		}
#endif
	}
	return true;
}

void
encode_html(int fd, char *str)
{
	char buf[1024];
	char *r, *w;

	for (r = str, w = buf; *r; r++) {
		switch (*r) {
		case '<': memcpy(w, "&lt;", 4); w += 4; break;
		case '>': memcpy(w, "&gt;", 4); w += 4; break;
		case '\n': memcpy(w, "\n<br/>\n", 7); w += 7; break;
		default: *w++ = *r;
		}
		if (w + 7 > buf + sizeof buf) {
			write(fd, buf, w - buf);
			w = buf;
		}
	}
	write(fd, buf, w - buf);
}

void
write_html(int fd)
{
	dprintf(fd, "%s", html_header1);
	encode_html(fd, mail.subject);
	dprintf(fd, "%s", html_header2);
	dprintf(fd, "<h1>");
	encode_html(fd, mail.subject);
	dprintf(fd, "</h1>\n");
	dprintf(fd, "<b>From:</b> ");
	encode_html(fd, mail.author);
	dprintf(fd, "<br/>\n<b>Date:</b> ");
	encode_html(fd, mail.date);
	dprintf(fd, "<br/>\n<hr/>\n");
	encode_html(fd, mail.content);
	dprintf(fd, "%s", html_footer);
}

int
main(int argc, char **argv)
{
	if (argc != 2) return 1;

	/* TODO error checking */
	int fd = open(argv[1], O_RDONLY);
	struct stat meta;
	fstat(fd, &meta);
	char *content = malloc(meta.st_size + 1);
	read(fd, content, meta.st_size);
	content[meta.st_size] = '\0';
	close(fd);
	
	char *pointer = content;
	if (!parse_header(&pointer, parse_field)) {
		fprintf(stderr, "can't parse header\n");
	} else {
		mail.content = pointer;
		decode_qprintable(mail.content);
		write_html(1);
	}

	free(content);
	return 0;
}

