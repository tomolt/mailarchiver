/* See LICENSE file for copyright and license details. */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <errno.h>
#include <unistd.h>

#include "util.h"

void
die(const char *format, ...)
{
	int err;
	va_list ap;
	err = errno;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	if (*format && format[strlen(format)-1] == ':') {
		fputc(' ', stderr);
		fputs(strerror(err), stderr);
	}
	fputc('\n', stderr);
	exit(1);
}

/* Like strcspn(), but takes explicit maximum lengths instead of relying on NUL termination. */
size_t
mem_cspn(const char *hay, size_t haylen, const char *needle, size_t needlelen)
{
	enum { UBITS = 8 * sizeof (unsigned) };
	typedef const unsigned char *BYTEP;
	BYTEP chr;
	unsigned bitfield[256 / UBITS] = { 0 };
	for (chr = (BYTEP) needle; chr < (BYTEP) needle + needlelen; chr++)
		bitfield[*chr / UBITS] |= 1u << (*chr % UBITS);
	for (chr = (BYTEP) hay; chr < (BYTEP) hay + haylen; chr++)
		if (bitfield[*chr / UBITS] & (1u << (*chr % UBITS))) break;
	return chr - (BYTEP) hay;
}

/* same as write(), but calls die() if the write fails. Also deals with EINTR */
ssize_t
check_write(int fd, const void *buf, size_t n)
{
	ssize_t ret, w = 0;

	for (w = 0; w < n; w += ret) {
		ret = write(fd, (char *)buf + w, n - w);
		if (ret < 0 && errno != EINTR)
			die("write():");
		ret = ret < 0 ? 0 : ret; /* don't increment `w` with negative value */
	}

	return w;
}

