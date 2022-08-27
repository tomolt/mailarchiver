#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "util.h"
#include "smakdir.h"

//static char *log_mem;
//static size_t log_length;

size_t
add_to_log(const char *info[])
{
	struct stat meta;
	int fd, i;
	if ((fd = open("log", O_WRONLY | O_APPEND)) < 0)
		die("cannot open central log file.");
	if (fstat(fd, &meta) < 0)
		die("fstat():");
	/* TODO reduce amount of syscalls */
	check_write(fd, info[0], strlen(info[0]));
	for (i = 1; i < MNUMINFO; i++) {
		check_write(fd, "\t", 1);
		check_write(fd, info[i], strlen(info[i]));
	}
	check_write(fd, "\n", 1);
	close(fd);
	return meta.st_size;
}

#if 0
map_log()
{
}

unmap_log()
{
}

read_from_log(char *info[])
{
}
#endif

