/* See LICENSE file for copyright and license details. */

#include <stddef.h>
#include <sys/types.h>

struct tm;

void die(const char *format, ...);

/* Like strcspn(), but takes explicit maximum lengths instead of relying on NUL termination. */
size_t mem_cspn(const char *hay, size_t haylen, const char *needle, size_t needlelen);

/* Same as write(), but calls die() if the write fails. Also deals with EINTR */
ssize_t check_write(int fd, const void *buf, size_t n);
/* Same as read(), but calls die() if the read fails. Also deals with EINTR */
ssize_t check_read(int fd, void *buf, size_t n);

/* Similar to the GNU extension timegm(). Unlike timegm() it doesn't modify its input. */
time_t mkutctime(const struct tm *tm);
