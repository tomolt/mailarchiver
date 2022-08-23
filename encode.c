/* See LICENSE file for copyright and license details. */

#include <string.h>

#include <unistd.h>

#include "encode.h"
#include "util.h"

void
encode_html(int fd, char *mem, size_t length)
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

