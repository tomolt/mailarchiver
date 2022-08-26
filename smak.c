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
#include "encode.h"
#include "util.h"
#include "config.h"

struct mail {
	char *subject;
	char *from;
	char *to;
	char *message_id;
	char *in_reply_to;
	char *body;
	size_t length; /* of the body */
	char tenc; /* transfer encoding: \0=raw, Q=quoted-printable, B=base64 */
	time_t date;
};

char *argv0;

static char *aether_base;
static char *aether_cursor;
static struct mail mail;
static int cachefd;

bool
process_field(char *key, char *value)
{
	struct tm tm;
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
		if (!parse_date(value, &tm)) return false;
		mail.date = mkutctime(&tm);
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
update_metacache(const char *msgpath)
{
	char date[200];
	/* TODO handle fcntl EINTR */
	struct flock lock = { 0 };
	lock.l_type   = F_WRLCK;
	lock.l_whence = SEEK_SET;
	if (fcntl(cachefd, F_SETLKW, &lock) < 0)
		die("fcntl():");
	strftime(date, sizeof date, "%Y-%m-%d %T", gmtime(&mail.date));
	dprintf(cachefd, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
		msgpath, mail.message_id, date, mail.from, mail.to,
		mail.in_reply_to ? mail.in_reply_to : "", mail.subject);
	lock.l_type = F_UNLCK;
	if (fcntl(cachefd, F_SETLK, &lock) < 0)
		die("fcntl():");
}

void
generate_html(const char *uniq)
{
	char tmppath[MAX_FILENAME_LENGTH];
	char wwwpath[MAX_FILENAME_LENGTH];
	char date[200];
	int fd;

	strcpy(tmppath, "tmp_www_XXXXXX");
	if ((fd = mkstemp(tmppath)) < 0)
		die("cannot create temporary file:");
	if (chmod(tmppath, 0640) < 0)
		die("chmod():");
	if (snprintf(wwwpath, MAX_FILENAME_LENGTH, "www/%s.html", uniq) >= MAX_FILENAME_LENGTH)
		die("file path is too long.");

	strftime(date, sizeof date, "%Y-%m-%d %T", gmtime(&mail.date));

	dprintf(fd, "%s", html_header1);
	encode_html(fd, mail.subject, strlen(mail.subject));
	dprintf(fd, "%s", html_header2);
	dprintf(fd, "<h1>");
	encode_html(fd, mail.subject, strlen(mail.subject));
	dprintf(fd, "</h1>\n");
	dprintf(fd, "<b>From:</b> ");
	encode_html(fd, mail.from, strlen(mail.from));
	dprintf(fd, "<br/>\n<b>Date:</b> ");
	encode_html(fd, date, strlen(date));
	dprintf(fd, "<br/>\n<hr/>\n<pre>");
	encode_html(fd, mail.body, mail.length);
	dprintf(fd, "</pre>\n%s", html_footer);

	close(fd);
	if (rename(tmppath, wwwpath) < 0)
		die("rename():");
}

bool
process_msg(const char *msgpath, const char *uniq)
{
	int fd;
	struct stat meta;
	char *text, *ptr;

	memset(&mail, 0, sizeof mail);

	if ((fd = open(msgpath, O_RDONLY)) < 0)
		die("cannot open '%s':", msgpath);

	if (fstat(fd, &meta) < 0)
		die("cannot stat '%s':", msgpath);

	text = mmap(NULL, meta.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (text == MAP_FAILED)
		die("mmap:");
	close(fd);

	if (!split_header_from_body(text, meta.st_size, &mail.body))
		return false;
	mail.length = meta.st_size - (mail.body - text);

	{
		char *pointer = text, *key, *value;
		while (*pointer) {
			if (!next_header_field(&pointer, &key, &value))
				return false;
			if (!process_field(key, value))
				return false;
		}
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
	update_metacache(msgpath);

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
		die("cannot open directory 'new':");

	while ((errno = 0, ent = readdir(dir))) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;

		if (snprintf(newpath, MAX_FILENAME_LENGTH, "new/%s", ent->d_name) >= MAX_FILENAME_LENGTH)
			die("file path is too long.");
		if (process_msg(newpath, ent->d_name)) {
			if (snprintf(curpath, MAX_FILENAME_LENGTH, "cur/%s:2,a", ent->d_name) >= MAX_FILENAME_LENGTH)
				die("file path is too long.");
		} else {
			if (snprintf(curpath, MAX_FILENAME_LENGTH, "cur/%s:2,e", ent->d_name) >= MAX_FILENAME_LENGTH)
				die("file path is too long.");
		}
		rename(newpath, curpath);
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

	if ((cachefd = open(metacache, O_CREAT | O_RDWR | O_APPEND, 0640)) < 0)
		die("cannot open meta-cache file.");

	aether_base = mmap(NULL, MAX_AETHER_MEMORY, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	aether_cursor = aether_base;

	process_new_dir();

	munmap(aether_base, MAX_AETHER_MEMORY);
	close(cachefd);
	return 0;
}

