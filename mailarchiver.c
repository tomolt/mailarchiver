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
print_field(char *key, char *value)
{
	if (!strcasecmp(key, "From") ||
	    !strcasecmp(key, "Subject") ||
	    !strcasecmp(key, "Date") ||
	    !strcasecmp(key, "Message-ID") ||
	    !strcasecmp(key, "References") ||
	    !strcasecmp(key, "In-Reply-To") ||
	    !strcasecmp(key, "Content-Type") ||
	    !strcasecmp(key, "Content-Transfer-Encoding")) {
		normalize_ws(value);
		fprintf(stderr, "%s is %s\n", key, value);
	}
	return true;
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
write_html(int fd, char *content)
{
	char buf[1024];
	char *r, *w;

	dprintf(fd, "%stitle%s", html_header1, html_header2);

	for (r = content, w = buf; *r; r++) {
		
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
	if (!parse_header(&pointer, print_field)) {
		fprintf(stderr, "can't parse header\n");
	} else {
		write_html(1, pointer);
	}

	free(content);
	return 0;
}

