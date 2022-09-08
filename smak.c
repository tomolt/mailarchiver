/* See LICENSE file for copyright and license details.
 *
 * A mailing list web archiver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>

#include "arg.h"
#include "mail.h"
#include "util.h"
#include "smakdir.h"
#include "config.h"

extern void generate_html(const char *uniq, const char *info[], char *body, size_t length);
extern void generate_html_report(const struct report *rpt);

char *argv0;

char *aether_base;
char *aether_cursor;

/* tenc = transfer encoding: \0=raw, Q=quoted-printable, B=base64 */
bool
process_header(char *header, const char *info[], char *tenc)
{
	char *key, *value, *str;
	struct token token;
	struct tm tm;

	*tenc = '\0';
	while (*header) {
		if (!next_header_field(&header, &key, &value))
			return false;
		if (!strcasecmp(key, "From")) {
			collapse_ws(value);
			if (!(str = convert_encwords(value))) return false;
			info[MFROM] = str;
		} else if (!strcasecmp(key, "Subject")) {
			collapse_ws(value);
			if (!(str = convert_encwords(value))) return false;
			info[MSUBJECT] = str;
		} else if (!strcasecmp(key, "Date")) {
			if (!parse_date(value, &tm)) return false;
			if (aether_cursor - aether_base + 32 > MAX_AETHER_MEMORY)
				die("not enough aether memory.");
			snprintf(aether_cursor, 32, "%lld", (long long) mkutctime(&tm));
			info[MTIME] = aether_cursor;
		} else if (!strcasecmp(key, "Message-ID")) {
			collapse_ws(value);
			info[MMSGID] = value;
		} else if (!strcasecmp(key, "In-Reply-To")) {
			collapse_ws(value);
			info[MINREPLYTO] = value;
		} else if (!strcasecmp(key, "Content-Transfer-Encoding")) {
			token = TOKEN_INIT(value);
			if (tokenize(&token) != TOKEN_ATOM) return false;
			if (!strcasecmp(token.atom, "7bit")) {
				*tenc = '\0';
			} else if (!strcasecmp(token.atom, "8bit")) {
				*tenc = '\0';
			} else if (!strcasecmp(token.atom, "binary")) {
				*tenc = '\0';
			} else if (!strcasecmp(token.atom, "quoted-printable")) {
				*tenc = 'Q';
			} else if (!strcasecmp(token.atom, "base64")) {
				*tenc = 'B';
			} else {
				return false;
			}
		}
	}
	return true;
}

bool
process_msg(const char *msgpath, const char *uniq)
{
	const char *info[MNUMINFO];
	struct stat meta;
	char *text, *ptr;
	int fd;
	char *body;
	size_t length;
	char tenc;

	memset(info, 0, sizeof info);
	info[MUNIQ] = uniq;
	info[MSUBJECT] = "(no subject)";
	info[MFROM] = "(no sender)";
	info[MMSGID] = "";
	info[MINREPLYTO] = "";
	info[MTIME] = "-1";

	if ((fd = open(msgpath, O_RDONLY)) < 0)
		die("cannot open '%s':", msgpath);

	if (fstat(fd, &meta) < 0)
		die("cannot stat '%s':", msgpath);

	text = mmap(NULL, meta.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (text == MAP_FAILED)
		die("mmap():");
	close(fd);

	if (!split_header_from_body(text, meta.st_size, &body))
		return false;
	length = meta.st_size - (body - text);

	if (!process_header(text, info, &tenc))
		return false;

	switch (tenc) {
	case 'Q':
		ptr = decode_qprintable(body, body, length);
		if (!ptr) return false;
		length = ptr - body;
		break;
	
	case 'B':
		ptr = decode_base64(body, body, length);
		if (!ptr) return false;
		length = ptr - body;
		break;
	}

	generate_html(uniq, info, body, length);
	MSG msg = add_to_log(info);

	time_t time = atoll(info[MTIME]);
	struct tm *tm = gmtime(&time);
	struct report rpt;

	/* TODO generate reports later on; only regenerate dirty reports once; only map log once. */
	map_log();
	read_report(&rpt, tm->tm_year + 1900, tm->tm_mon + 1);
	add_to_report(&rpt, time, msg);
	write_report(&rpt);
	generate_html_report(&rpt);
	close_report(&rpt);
	unmap_log();

	munmap(text, meta.st_size);
	return true;
}

void
process_new_dir(void)
{
	DIR *dir;
	struct dirent *ent;
	char uniq[MAX_FILENAME_LENGTH];
	char newpath[MAX_FILENAME_LENGTH];
	char curpath[MAX_FILENAME_LENGTH];
	char *colon;

	if (!(dir = opendir("new")))
		die("cannot open directory 'new':");

	while ((errno = 0, ent = readdir(dir))) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;

		colon = strrchr(ent->d_name, ':');
		if (colon) {
			/* FIXME potential buffer overrun */
			memcpy(uniq, ent->d_name, colon - ent->d_name);
			uniq[colon - ent->d_name] = '\0';
		} else {
			/* FIXME snprintf() is overkill */
			snprintf(uniq, MAX_FILENAME_LENGTH, "%s", ent->d_name);
		}

		if (snprintf(newpath, MAX_FILENAME_LENGTH, "new/%s", ent->d_name) >= MAX_FILENAME_LENGTH)
			die("file path is too long.");
		if (process_msg(newpath, uniq)) {
			if (snprintf(curpath, MAX_FILENAME_LENGTH, "cur/%s:2,a", uniq) >= MAX_FILENAME_LENGTH)
				die("file path is too long.");
		} else {
			if (snprintf(curpath, MAX_FILENAME_LENGTH, "cur/%s:2,e", uniq) >= MAX_FILENAME_LENGTH)
				die("file path is too long.");
		}
		rename(newpath, curpath);

		/* clear aether after every message */
		aether_cursor = aether_base;
	}
	if (errno)
		die("readdir():");

	closedir(dir);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [maildir]\n", argv0);
}

int
main(int argc, char **argv)
{
	struct stat meta;

	ARGBEGIN {
	default:
		usage();
		exit(1);
	} ARGEND
	if (argc) {
		if (chdir(*argv) < 0)
			die("cannot go to directory:");
		argc--, argv++;
	}
	if (argc) {
		usage();
		exit(1);
	}

	aether_base = mmap(NULL, MAX_AETHER_MEMORY, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	aether_cursor = aether_base;

	init_smakdir();

	if (stat("www", &meta) < 0 || !S_ISDIR(meta.st_mode))
		die("You need to create or link a 'www/' subdirectory.");

	process_new_dir();

	munmap(aether_base, MAX_AETHER_MEMORY);
	return 0;
}

