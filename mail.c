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

#include <string.h>
#include <time.h>

#include "mail.h"
#include "util.h"

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
next_header_field(char **pointer, char **key, char **value)
{
	char *cursor = *pointer;

	if (!*cursor) return false;

	if (!is_key(*cursor)) return false;
	*key = cursor;
	do cursor++; while (is_key(*cursor));
	if (*cursor != ':') return false;
	*cursor++ = '\0';

	*value = cursor;
	do {
		if (!(cursor = strchr(cursor, '\n')))
			return false;
		cursor++;
	} while (is_ws(*cursor) && *cursor != '\n');
	*(cursor-1) = '\0';

	*pointer = cursor;
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
	char *whead;

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
		token->atom = whead = token->rhead;
		for (;;) {
			switch (*token->rhead) {
			case '\0': return TOKEN_ERROR;
			case '"':
				token->rhead++;
				*whead = '\0';
				collapse_ws(token->atom);
				return TOKEN_ATOM;
			case '\\':
				token->rhead++;
				if (!*token->rhead) return TOKEN_ERROR;
				/* fallthrough */
			default:
				*whead++ = *token->rhead++;
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

static bool
read_decimal(const char *atom, int ndigits, int *value)
{
	int d;
	*value = 0;
	for (d = 0; d < ndigits; d++) {
		if (!(atom[d] >= '0' && atom[d] <= '9')) return false;
		*value *= 10;
		*value += atom[d] - '0';
	}
	return atom[d] == '\0';
}

static bool
parse_decimal(struct token *tok, int ndigits, int *value)
{
	return tokenize(tok) == TOKEN_ATOM && read_decimal(tok->atom, ndigits, value);
}

bool
parse_date(char *date, struct tm *tm)
{
	const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct token tok = TOKEN_INIT(date);
	
	/* skip over weekday name if present */
	if (tokenize(&tok) != TOKEN_ATOM) return false;
	if (!(tok.atom[0] >= '0' && tok.atom[0] <= '9')) {
		if (tokenize(&tok) != ',') return false;
		if (tokenize(&tok) != TOKEN_ATOM) return false;
	}

	/* day, month, year */
	if (!read_decimal(tok.atom, 2, &tm->tm_mday)) return false;
	for (tm->tm_mon = 0; tm->tm_mon < 12; tm->tm_mon++) {
		if (!strcmp(tok.atom, months[tm->tm_mon])) break;
	}
	if (tm->tm_mon == 12) return false;
	if (!parse_decimal(&tok, 2, &tm->tm_year)) return false;

	/* hour, minute */
	if (!parse_decimal(&tok, 2, &tm->tm_hour)) return false;
	if (tokenize(&tok) != ':') return false;
	if (!parse_decimal(&tok, 2, &tm->tm_min)) return false;
	
	/* second (optional) */
	if (tokenize(&tok) == ':') {
		if (!parse_decimal(&tok, 2, &tm->tm_sec)) return false;
	}

	return tokenize(&tok) == TOKEN_END;
}

