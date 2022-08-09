#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

#include <dirent.h>
#include <errno.h>

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

#if 0
bool
parse_header()
{
}

bool
parse_mail(bool (*header_cb)())
{

}
#endif

void
process_inbox(const char *dirname)
{
	DIR *dir;
	struct dirent *ent;

	dir = opendir(dirname);
	if (!dir) die("opendir(\"%s\"): %s", dirname, strerror(errno));

	while ((errno = 0, ent = readdir(dir))) {
		if (ent->d_name[0] == '.') continue;
		printf("%s\n", ent->d_name);
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

