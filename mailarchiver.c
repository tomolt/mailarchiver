/* See LICENSE file for copyright and license details.
 *
 * A mailing list web archiver
 *
 * Implementation Note:
 *
 * Parsing & decoding generates a lot of strings. Instead of allocating space
 * for each output string, we do (nearly) all string transforms in-place. The
 * typical idiom for this is to keep two pointers into memory, rhead and whead
 * (the 'read' and 'write' head respectively). We can read bytes from rhead and
 * write bytes to whead without issue as long as the following holds:
 * Every time whead is incremented, rhead is also moved by at least one byte.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>

#include "config.h"

struct mail {
	char *subject;
	char *from;
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
	/* TODO cover all types of whitespace allowed in mail header */
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
			/* TODO this can be implemented more cleanly */
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

/* Decode any 'Encoded Words' of the form =?charset?encoding?content?=
 * that may appear in header fields. */
bool
decode_encwords(char *str)
{
	char *rhead, *whead, *mark;
	char encoding;
	char *c;

	rhead = whead = str;
	while ((mark = strstr(rhead, "=?"))) {
		memcpy(whead, rhead, mark - rhead);
		whead += mark - rhead;
		rhead = mark + 2;

		if (!(mark = strchr(rhead, '?'))) return false;
		rhead = mark + 1;

		if (*rhead != 'Q' && *rhead != 'B') return false;
		encoding = *rhead++;
		if (*rhead != '?') return false;
		rhead++;

		if (!(mark = strchr(rhead, '?'))) return false;
		if (mark[1] != '=') return false;

		*mark = '\0';
		if (encoding == 'Q') {
			for (c = rhead; *c; c++) {
				if (*c == '_') *c = ' ';
			}
			if (!decode_qprintable(rhead)) return false;
		} else {
			if (!decode_base64(rhead)) return false;
		}
		memcpy(whead, rhead, strlen(rhead));
		whead += strlen(rhead);

		rhead = mark + 2;
	}
	memcpy(whead, rhead, strlen(rhead));
	whead += strlen(rhead);
	*whead = '\0';
	return true;
}

bool
process_field(char *key, char *value)
{
	if (!strcasecmp(key, "From")) {
		normalize_ws(value);
		if (!decode_encwords(value)) return false;
		mail.from = value;
	} else if (!strcasecmp(key, "Subject")) {
		normalize_ws(value);
		if (!decode_encwords(value)) return false;
		mail.subject = value;
	} else if (!strcasecmp(key, "Date")) {
		normalize_ws(value);
		if (!decode_encwords(value)) return false;
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
	encode_html(fd, mail.from);
	dprintf(fd, "<br/>\n<b>Date:</b> ");
	encode_html(fd, mail.date);
	dprintf(fd, "<br/>\n<hr/>\n");
	encode_html(fd, mail.content);
	dprintf(fd, "%s", html_footer);
}

int
main(int argc, char **argv)
{
	int fd;
	struct stat meta;
	char *text;

	if (argc != 2) return 1;

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) die("cannot open '%s': %s", argv[1], strerror(errno));

	if (fstat(fd, &meta) < 0) die("cannot stat '%s': %s", argv[1], strerror(errno));

	errno = 0;
	text = malloc(meta.st_size + 1);
	if (!text) die("malloc: %s", strerror(errno));

	/* TODO we should probably handle EINTR and partial reads */
	if (read(fd, text, meta.st_size) < 0)
		die("read: %s", strerror(errno));
	text[meta.st_size] = '\0';
	close(fd);
	
	mail.content = text;
	if (!parse_header(&mail.content, process_field))
		die("cannot parse mail header");

	if (!decode_qprintable(mail.content))
		die("cannot decode mail contents");

	write_html(1);

	free(text);
	return 0;
}

