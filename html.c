/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <sys/stat.h>

#include "util.h"
#include "smakdir.h"

#define CONFIG_HTML
#include "config.h"

static void
encode_html(int fd, const char *mem, size_t length)
{
	char buf[4096];
	char *w = buf;
	size_t idx = 0, run;

	for (;;) {
		run = mem_cspn(mem + idx, length - idx, "<>&\"\0", 5);
		if ((w - buf) + run > sizeof buf - 16) {
			check_write(fd, buf, w - buf);
			w = buf;
		}
		if (run > sizeof buf - 16) {
			check_write(fd, mem + idx, run);
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
	if (w > buf) check_write(fd, buf, w - buf);
}

void
generate_html(const char *uniq, const char *info[], char *body, size_t length)
{
	char tmppath[MAX_FILENAME_LENGTH];
	char wwwpath[MAX_FILENAME_LENGTH];
	time_t time;
	char date[100];
	int fd;

	strcpy(tmppath, "tmp_www_XXXXXX");
	if ((fd = mkstemp(tmppath)) < 0)
		die("cannot create temporary file:");
	if (chmod(tmppath, 0640) < 0)
		die("chmod():");
	if (snprintf(wwwpath, MAX_FILENAME_LENGTH, "www/%s.html", uniq) >= MAX_FILENAME_LENGTH)
		die("file path is too long.");

	time = atoll(info[MTIME]);
	strftime(date, sizeof date, "%Y-%m-%d %T", gmtime(&time));

	dprintf(fd, "%s", html_header1);
	encode_html(fd, info[MSUBJECT], strlen(info[MSUBJECT]));
	dprintf(fd, "%s", html_header2);
	dprintf(fd, "<h1>");
	encode_html(fd, info[MSUBJECT], strlen(info[MSUBJECT]));
	dprintf(fd, "</h1>\n");
	dprintf(fd, "<b>From:</b> ");
	encode_html(fd, info[MFROM], strlen(info[MFROM]));
	dprintf(fd, "<br/>\n<b>Date:</b> ");
	encode_html(fd, date, strlen(date));
	dprintf(fd, "<br/>\n<hr/>\n<pre>");
	encode_html(fd, body, length);
	dprintf(fd, "</pre>\n%s", html_footer);

	close(fd);
	if (rename(tmppath, wwwpath) < 0)
		die("rename():");
}
