/* See LICENSE file for copyright and license details. */

#include <stddef.h>

void die(const char *format, ...);

/* Like strcspn(), but takes explicit maximum lengths instead of relying on NUL termination. */
size_t memcspn(const char *hay, size_t haylen, const char *needle, size_t needlelen);

