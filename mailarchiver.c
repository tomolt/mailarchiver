#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>

#if 0
struct mailpart {
	char *mimetype;
	char *name; /* TODO rename this field */
	size_t length;
	char *data;
};

struct mail {
	char *uniq;
	char *author;
	char *title;
	char *date;
	size_t nparts;
	struct mailpart *parts;
};
#endif

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

/* TODO comment stripping function */

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
	    !strcasecmp(key, "To") ||
	    !strcasecmp(key, "Subject") ||
	    !strcasecmp(key, "Date") ||
	    !strcasecmp(key, "Content-Type") ||
	    !strcasecmp(key, "Content-Encoding")) {
		normalize_ws(value);
		printf("%s is %s\n", key, value);
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
process_inbox(const char *dirname)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir(dirname);
	if (!dir) die("opendir(\"%s\"): %s", dirname, strerror(errno));

	while ((errno = 0, ent = readdir(dir))) {
		if (ent->d_name[0] == '.') continue;
		printf("Regarding file %s:\n", ent->d_name);

		/* FIXME concat dirname with ent name */
		/* TODO error checking */
		int fd = open(ent->d_name, O_RDONLY);
		struct stat meta;
		fstat(fd, &meta);
		char *content = malloc(meta.st_size + 1);
		read(fd, content, meta.st_size);
		content[meta.st_size] = '\0';
		close(fd);
		
		char *pointer = content;
		if (!parse_header(&pointer, print_field)) {
			printf("can't parse header\n");
		}

		free(content);

		printf("\n\n\n");
	}
	if (errno) die("readdir(\"%s\"): %s", dirname, strerror(errno));

	closedir(dir);
}

int
main()
{
	process_inbox(".");
	return 0;
}

