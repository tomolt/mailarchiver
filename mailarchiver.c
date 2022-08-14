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
#include <dirent.h>

#include "arg.h"
#include "config.h"

#define TOKEN_INIT(ptr) (struct token) { ptr, NULL, -1 }
#define TOKEN_ATOM  256
#define TOKEN_END     0
#define TOKEN_ERROR  -1

struct mail {
	char *subject;
	char *from;
	char *date;
	char *to;
	char *message_id;
	char *in_reply_to;
	char *body;
	size_t length; /* of the body */
	char tenc; /* transfer encoding: \0=raw, Q=quoted-printable, B=base64 */
};

struct token {
	char *rhead;
	char *atom;
	int   evicted;
};

char *argv0;

static struct mail mail;
static bool do_print_meta;

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

/* Like strcspn(), but takes explicit maximum lengths instead of relying on NUL termination. */
size_t
memcspn(const char *hay, size_t haylen, const char *needle, size_t needlelen)
{
#define UBITS (8 * sizeof (unsigned))
	typedef const unsigned char *BYTEP;
	BYTEP chr;
	unsigned bitfield[256 / UBITS] = { 0 };
	for (chr = (BYTEP) needle; chr < (BYTEP) needle + needlelen; chr++)
		bitfield[*chr / UBITS] |= 1u << (*chr % UBITS);
	for (chr = (BYTEP) hay; chr < (BYTEP) hay + haylen; chr++)
		if ((bitfield[*chr / UBITS] >> (*chr % UBITS)) & 1u) break;
#undef UBITS
	return chr - (BYTEP) hay;
}

static inline bool
is_ws(char c)
{
	/* TODO Cover all types of whitespace allowed in mail header. */
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool
is_key(char c)
{
	/* There's likely even more allowed characters than this. */
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
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

/* Converts each run of whitespace in str to a single space. */
void
collapse_ws(char *str)
{
	char *src = str, *dst = str;
	while (is_ws(*src)) src++;
	while (*src) {
		*dst++ = *src++;
		if (is_ws(*src)) {
			do src++; while (is_ws(*src));
			if (*src) *dst++ = ' ';
		}
	}
	*dst = '\0';
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
		 * 'first' is still in memory, i.e. hasn't been evicted. */
		token->atom = token->rhead - 1;
		while (is_atom(*token->rhead)) token->rhead++;
		/* evict char after atom to make space for NUL terminator. */
		token->evicted = *token->rhead;
		*token->rhead = '\0';
		return TOKEN_ATOM;
	}

	return TOKEN_ERROR;
}

static int
decode_hex_digit(char c)
{
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= '0' && c <= '9') return c - '0';
	return -1;
}

char *
decode_qprintable(char *rhead, char *whead, size_t length)
{
	char *end = rhead + length, *eq;
	int lo, hi;

	while ((eq = memchr(rhead, '=', end - rhead))) {
		memmove(whead, rhead, eq - rhead);
		whead  += eq - rhead;
		rhead   = eq + 1;

		if (end - rhead >= 2
		&& (hi = decode_hex_digit(rhead[0])) >= 0
		&& (lo = decode_hex_digit(rhead[1])) >= 0) {
			*whead++ = hi * 16 + lo;
			rhead += 2;
		} else if (end - rhead >= 2 && rhead[0] == '\r' && rhead[1] == '\n') {
			rhead += 2;
		} else if (end - rhead >= 1 && rhead[0] == '\n') {
			rhead += 1;
		} else return NULL;
	}
	memmove(whead, rhead, end - rhead);
	whead += end - rhead;
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
	/* This implementation is inefficient, but it will suffice for now. */

	char *end = rhead + length;
	unsigned long value = 0;
	int digit, bits = 0;

	while (rhead < end && *rhead != '=') {
		if (is_ws(*rhead)) {
			rhead++;
			continue;
		}
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
	}
	return whead;
}

/* Decode any 'Encoded Words' of the form =?charset?encoding?content?=
 * that may appear in header fields. See RFC 2047. */
bool
decode_encwords(char *str)
{
	char *rhead, *whead, *mark, *c;
	char encoding;

	rhead = whead = str;
	while ((mark = strstr(rhead, "=?"))) {
		memmove(whead, rhead, mark - rhead);
		whead += mark - rhead;
		rhead = mark + 2;

		if (!(mark = strchr(rhead, '?'))) return false;
		rhead = mark + 1;

		if (*rhead != 'Q' && *rhead != 'q' && *rhead != 'B' && *rhead != 'b') return false;
		encoding = *rhead++;
		if (*rhead != '?') return false;
		rhead++;

		if (!(mark = strchr(rhead, '?'))) return false;
		if (mark[1] != '=') return false;

		if (encoding == 'Q' || encoding == 'q') {
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
	} else if (!strcasecmp(key, "To")) {
		collapse_ws(value);
		if (!decode_encwords(value)) return false;
		mail.to = value;
	} else if (!strcasecmp(key, "Message-ID")) {
		collapse_ws(value);
		if (!decode_encwords(value)) return false;
		mail.message_id = value;
	} else if (!strcasecmp(key, "In-Reply-To")) {
		collapse_ws(value);
		if (!decode_encwords(value)) return false;
		mail.in_reply_to = value;
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
write_meta(int fd, const char *msgpath)
{
	dprintf(fd, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
		msgpath, mail.message_id, mail.date, mail.from, mail.to,
		mail.in_reply_to ? mail.in_reply_to : "", mail.subject);
}

void
encode_html(int fd, char *mem, size_t length)
{
	char buf[4096];
	char *w = buf;
	size_t idx = 0, run;

	for (;;) {
		run = memcspn(mem + idx, length - idx, "<>&\"\0", 5);
		if ((w - buf) + run > sizeof buf - 16) {
			write(fd, buf, w - buf);
			w = buf;
		}
		if (run > sizeof buf - 16) {
			write(fd, mem + idx, run);
		} else {
			memcpy(w, mem + idx, run);
			w += run;
		}
		idx += run;
		if (idx == length) break;

		switch (mem[idx++]) {
		case '<': w = stpcpy(w, "&lt;"); break;
		case '>': w = stpcpy(w, "&gt;"); break;
		case '&': w = stpcpy(w, "&amp;"); break;
		case '"': w = stpcpy(w, "&quot;"); break;
		default:  w = stpcpy(w, "?");
		}
	}
	if (w > buf) write(fd, buf, w - buf);
}

void
generate_html(const char *uniq)
{
	char tmppath[MAX_FILENAME_LENGTH];
	char wwwpath[MAX_FILENAME_LENGTH];
	int fd;

	strcpy(tmppath, "tmp_www_XXXXXX");
	if ((fd = mkstemp(tmppath)) < 0)
		die("cannot create temporary file: %s", strerror(errno));
	if (chmod(tmppath, 0644) < 0)
		die("chmod(): %s", strerror(errno));
	if (snprintf(wwwpath, MAX_FILENAME_LENGTH, "www/%s.html", uniq) >= MAX_FILENAME_LENGTH)
		die("file path is too long.");

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
	dprintf(fd, "<br/>\n<hr/>\n<pre>");
	encode_html(fd, mail.body, mail.length);
	dprintf(fd, "</pre>\n%s", html_footer);

	close(fd);
	if (rename(tmppath, wwwpath) < 0)
		die("rename(): %s", strerror(errno));
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [-m] mail-file\n", argv0);
}

bool
process_msg(const char *msgpath, const char *uniq)
{
	int fd;
	struct stat meta;
	char *text, *ptr;

	memset(&mail, 0, sizeof mail);

	if ((fd = open(msgpath, O_RDONLY)) < 0)
		die("cannot open '%s': %s", msgpath, strerror(errno));

	if (fstat(fd, &meta) < 0)
		die("cannot stat '%s': %s", msgpath, strerror(errno));

	text = mmap(NULL, meta.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (text == MAP_FAILED)
		die("mmap: %s", strerror(errno));
	close(fd);

	if (!split_header_from_body(text, meta.st_size, &mail.body))
		return false;
	mail.length = meta.st_size - (mail.body - text);

	if (!parse_header(text, process_field))
		return false;

	if (do_print_meta) {
		write_meta(1, msgpath);
		return true;
	}

	switch (mail.tenc) {
	case 'Q':
		ptr = decode_qprintable(mail.body, mail.body, mail.length);
		if (!ptr) return false;
		mail.length = ptr - mail.body;
		break;
	
	case 'B':
		ptr = decode_base64(mail.body, mail.body, mail.length);
		if (!ptr) return false;
		mail.length = ptr - mail.body;
		break;
	}

	generate_html(uniq);

	munmap(text, meta.st_size);
	return true;
}

void
process_new_dir(void)
{
	DIR *dir;
	struct dirent *ent;
	char newpath[MAX_FILENAME_LENGTH];
	char curpath[MAX_FILENAME_LENGTH];

	if (!(dir = opendir("new")))
		die("cannot open directory 'new': %s", strerror(errno));

	while ((errno = 0, ent = readdir(dir))) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;

		if (snprintf(newpath, MAX_FILENAME_LENGTH, "new/%s", ent->d_name) >= MAX_FILENAME_LENGTH)
			die("file path is too long.");
		if (process_msg(newpath, ent->d_name)) {
			if (snprintf(curpath, MAX_FILENAME_LENGTH, "cur/%s:2,E", ent->d_name) >= MAX_FILENAME_LENGTH)
				die("file path is too long.");
		} else {
			if (snprintf(curpath, MAX_FILENAME_LENGTH, "cur/%s:2,S", ent->d_name) >= MAX_FILENAME_LENGTH)
				die("file path is too long.");
		}
		rename(newpath, curpath);
	}
	if (errno)
		die("readdir(): %s", strerror(errno));

	closedir(dir);
}

int
main(int argc, char **argv)
{
	ARGBEGIN {
	case 'm':
		do_print_meta = true;
		break;
	default:
		usage();
		exit(1);
	} ARGEND
	if (argc) {
		if (chdir(*argv) < 0)
			die("cannot go to directory: %s", strerror(errno));
		argc--, argv++;
	}
	if (argc) {
		usage();
		exit(1);
	}

	process_new_dir();

	return 0;
}

