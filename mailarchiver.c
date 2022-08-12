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
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>

#include "config.h"

#define TOKEN_INIT(ptr) (struct token) { ptr, NULL, -1 }
#define TOKEN_ATOM  256
#define TOKEN_END     0
#define TOKEN_ERROR  -1

struct mail {
	char *subject;
	char *from;
	char *date;
	char *body;
	size_t length; /* of the body */
	char tenc; /* transfer encoding: \0=raw, Q=quoted-printable, B=base64 */
};

struct token {
	char *rhead;
	char *atom;
	int   evicted;
};

static struct mail mail;

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

static inline bool
is_special(char c)
{
	return c && (strchr("<>[]:;@\\,", c) != NULL);
}

static inline bool
is_atom(char c)
{
	if (c >= 'a' && c <= 'z') return true;
	if (c >= 'A' && c <= 'Z') return true;
	if (c >= '0' && c <= '9') return true;
	return c && (strchr("!#$%&'*+-/=?^_`{|}~.", c) != NULL);
}

bool
split_header_from_body(char *msg, size_t length, char **body)
{
	char *pos = msg;

	while ((pos = memchr(pos, '\n', length - (pos - msg)))) {
		pos++;
		if (pos < msg + length && pos[0] == '\n') {
			*pos = '\0';
			*body = pos + 1;
			return true;
		}
		if (pos < msg + length - 1 && pos[0] == '\r' && pos[1] == '\n') {
			*pos = '\0';
			*body = pos + 2;
			return true;
		}
	}
	return false;
}

bool
parse_header(char *header, bool (*field_cb)(char *key, char *value))
{
	char *cursor = header;
	char *key, *value;

	while (*cursor) {
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
	return true;
}

void
collapse_ws(char *str)
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

static bool
skip_comment(char **pointer, int depth)
{
	char *cursor = *pointer;
	do {
		switch (*cursor++) {
		case '\0': return false;
		case '\\': if (!*cursor++) return false;
		case '(':  depth++; break;
		case ')':  depth--; break;
		}
	} while (depth);
	*pointer = cursor;
	return true;
}

int
tokenize(struct token *token)
{
	char first;

restart:
	first = token->evicted < 0 ? *token->rhead++ : token->evicted;
	token->evicted = -1;

	if (!first) return TOKEN_END;
	if (is_ws(first)) goto restart;
	if (first == '(') {
		if (!skip_comment(&token->rhead, 1))
			return TOKEN_ERROR;
		goto restart;
	}

	/* special char */
	if (is_special(first))
		return first;

	/* quoted string */
	if (first == '"') {
		token->atom = token->rhead;
		for (;;) {
			switch (*token->rhead++) {
			case '\0': return TOKEN_ERROR;
			case '\\': if (!*token->rhead++) return TOKEN_ERROR;
			case '"':
				   *(token->rhead-1) = '\0';
				   collapse_ws(token->atom);
				   return TOKEN_ATOM;
			}
		}
	}

	/* atom */
	if (is_atom(first)) {
		/* no two atoms can come directly one after another, meaning
		 * first is still in memory, i.e. hasn't been evicted. */
		token->atom = token->rhead - 1;
		while (is_atom(*token->rhead)) token->rhead++;
		/* evict char after atom to make space for NUL terminator. */
		token->evicted = *token->rhead;
		*token->rhead = '\0';
		return TOKEN_ATOM;
	}

	return TOKEN_ERROR;
}

char *
decode_qprintable(char *rhead, char *whead, size_t length)
{
	char *eq;

	while ((eq = strchr(rhead, '='))) {
		memmove(whead, rhead, eq - rhead);
		whead += eq - rhead;
		length -= eq - rhead + 1;
		rhead = eq + 1;

		if (length >= 2 && is_hex(rhead[0]) && is_hex(rhead[1])) {
			/* TODO this can be implemented more cleanly */
			*whead++ = (rhead[0] >= 'A' ? rhead[0] - 'A' + 10 : rhead[0] - '0') * 16 +
				(rhead[1] >= 'A' ? rhead[1] - 'A' + 10 : rhead[1] - '0');
			rhead += 2;
			length -= 2;
		} else if (length >= 2 && rhead[0] == '\r' && rhead[1] == '\n') {
			rhead += 2;
			length -= 2;
		} else if (length >= 1 && rhead[0] == '\n') {
			rhead += 1;
			length -= 1;
		} else return NULL;
	}
	memmove(whead, rhead, length);
	whead += length;
	return whead;
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

char *
decode_base64(char *rhead, char *whead, size_t length)
{
	/* This implementation is terribly inefficient, but it should suffice for now. */

	unsigned long value;
	int digit;
	int bits;

	value = 0;
	bits = 0;
	while (length && *rhead != '=') {
		digit = decode_base64_digit(*rhead);
		if (digit < 0) return NULL;
		value <<= 6;
		value |= digit;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			*whead++ = value >> bits;
			value &= (1u << bits) - 1u;
		}
		rhead++;
		length--;
	}
	return whead;
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
		memmove(whead, rhead, mark - rhead);
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

		if (encoding == 'Q') {
			for (c = rhead; c < mark; c++) {
				if (*c == '_') *c = ' ';
			}
			whead = decode_qprintable(rhead, whead, mark - rhead);
			if (!whead) return false;
		} else {
			whead = decode_base64(rhead, whead, mark - rhead);
			if (!whead) return false;
		}

		rhead = mark + 2;
	}
	memmove(whead, rhead, strlen(rhead));
	whead += strlen(rhead);
	*whead = '\0';
	return true;
}

bool
process_field(char *key, char *value)
{
	struct token token;

	if (!strcasecmp(key, "From")) {
		collapse_ws(value);
		if (!decode_encwords(value)) return false;
		mail.from = value;
	} else if (!strcasecmp(key, "Subject")) {
		collapse_ws(value);
		if (!decode_encwords(value)) return false;
		mail.subject = value;
	} else if (!strcasecmp(key, "Date")) {
		collapse_ws(value);
		if (!decode_encwords(value)) return false;
		mail.date = value;
	} else if (!strcasecmp(key, "Content-Transfer-Encoding")) {
		token = TOKEN_INIT(value);
		if (tokenize(&token) != TOKEN_ATOM) return false;
		if (!strcasecmp(token.atom, "7bit")) {
			mail.tenc = '\0';
		} else if (!strcasecmp(token.atom, "8bit")) {
			mail.tenc = '\0';
		} else if (!strcasecmp(token.atom, "binary")) {
			mail.tenc = '\0';
		} else if (!strcasecmp(token.atom, "quoted-printable")) {
			mail.tenc = 'Q';
		} else if (!strcasecmp(token.atom, "base64")) {
			mail.tenc = 'B';
		} else {
			return false;
		}
	}
	return true;
}

void
encode_html(int fd, char *mem, size_t length)
{
	char buf[1024];
	char *r, *w;

	for (r = mem, w = buf; r < mem + length; r++) {
		switch (*r) {
		case '<':  w = stpcpy(w, "&lt;"); break;
		case '>':  w = stpcpy(w, "&gt;"); break;
		case '&':  w = stpcpy(w, "&amp;"); break;
		case '"':  w = stpcpy(w, "&quot;"); break;
		case '\0': w = stpcpy(w, "?NUL?"); break;
		case '\n': w = stpcpy(w, "\n<br/>\n"); break;
		case '\t': w = stpcpy(w, "&nbsp;&nbsp;&nbsp;&nbsp;"); break;
		default: *w++ = *r;
		}
		if (buf + sizeof buf - w <= 24) {
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
	encode_html(fd, mail.subject, strlen(mail.subject));
	dprintf(fd, "%s", html_header2);
	dprintf(fd, "<h1>");
	encode_html(fd, mail.subject, strlen(mail.subject));
	dprintf(fd, "</h1>\n");
	dprintf(fd, "<b>From:</b> ");
	encode_html(fd, mail.from, strlen(mail.from));
	dprintf(fd, "<br/>\n<b>Date:</b> ");
	encode_html(fd, mail.date, strlen(mail.date));
	dprintf(fd, "<br/>\n<hr/>\n");
	encode_html(fd, mail.body, mail.length);
	dprintf(fd, "%s", html_footer);
}

int
main(int argc, char **argv)
{
	int fd;
	struct stat meta;
	char *text, *ptr;

	if (argc != 2) return 1;

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) die("cannot open '%s': %s", argv[1], strerror(errno));

	if (fstat(fd, &meta) < 0) die("cannot stat '%s': %s", argv[1], strerror(errno));

	text = mmap(NULL, meta.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (text == MAP_FAILED)
		die("mmap: %s", strerror(errno));
	close(fd);
	
	if (!split_header_from_body(text, meta.st_size, &mail.body))
		die("cannot discern mail header from body");
	mail.length = meta.st_size - (mail.body - text);

	if (!parse_header(text, process_field))
		die("cannot parse mail header");

	switch (mail.tenc) {
	case 'Q':
		ptr = decode_qprintable(mail.body, mail.body, mail.length);
		if (!ptr) die("cannot decode mail contents");
		mail.length = ptr - mail.body;
		break;
	
	case 'B':
		ptr = decode_base64(mail.body, mail.body, mail.length);
		if (!ptr) die("cannot decode mail contents");
		mail.length = ptr - mail.body;
		break;
	}

	write_html(1);

	munmap(text, meta.st_size);
	return 0;
}

