#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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

int
main()
{
	return 0;
}

