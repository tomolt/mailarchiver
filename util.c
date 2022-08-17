/* See LICENSE file for copyright and license details. */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <errno.h>

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
memcspn(const char *hay, size_t haylen, const char *needle, size_t needlelen)
{
#define UBITS (8 * sizeof (unsigned))
	typedef const unsigned char *BYTEP;
	BYTEP chr;
	unsigned bitfield[256 / UBITS] = { 0 };
	for (chr = (BYTEP) needle; chr < (BYTEP) needle + needlelen; chr++)
		bitfield[*chr / UBITS] |= 1u << (*chr % UBITS);
	for (chr = (BYTEP) hay; chr < (BYTEP) hay + haylen; chr++)
		if (bitfield[*chr / UBITS] & (1u << (*chr % UBITS))) break;
#undef UBITS
	return chr - (BYTEP) hay;
}

