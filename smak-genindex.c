#include <string.h>
#include <stdio.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "config.h"
#include "encode.h"
#include "util.h"

int
main()
{
	int fd;

	if ((fd = open("www/index.html", O_WRONLY | O_CREAT, 0640)) < 0)
		die("cannot open 'www/index.html': %s", strerror(errno));

	dprintf(fd, "%s", html_header1);
	dprintf(fd, "%s", html_header2);

	dprintf(fd, "%s", html_footer);

	close(fd);
	return 0;
}

