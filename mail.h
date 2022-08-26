/* See LICENSE file for copyright and license details. */

#include <stddef.h>
#include <stdbool.h>

struct tm;

#define TOKEN_INIT(ptr) (struct token) { ptr, NULL, -1 }
#define TOKEN_ATOM  256
#define TOKEN_END     0
#define TOKEN_ERROR  -1

struct token {
	char *rhead;
	char *atom;
	int   evicted;
};

bool split_header_from_body(char *msg, size_t length, char **body);
bool next_header_field(char **pointer, char **key, char **value);

/* Converts each run of whitespace in str to a single space. */
void collapse_ws(char *str);

int tokenize(struct token *token);

char *decode_qprintable(char *rhead, char *whead, size_t length);
char *decode_base64(char *rhead, char *whead, size_t length);
/* Decode any 'Encoded Words' of the form =?charset?encoding?content?=
 * that may appear in header fields. See RFC 2047. */
bool decode_encwords(char *str);

bool parse_date(char *date, struct tm *tm);

