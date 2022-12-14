/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "util.h"
#include "smakdir.h"
#include "config.h"

extern char *aether_base;
extern char *aether_cursor;

static char  *log_base;
static size_t log_length;

void
init_smakdir(void)
{
	struct stat meta;

	if (stat("smak", &meta) >= 0 && S_ISDIR(meta.st_mode))
		return;

	if (mkdir("smak", 0750) < 0)
		die("mkdir():");
	if (mkdir("smak/report", 0750) < 0)
		die("mkdir():");
}

size_t
add_to_log(const char *info[])
{
	struct stat meta;
	int fd, i;
	if ((fd = open("smak/log", O_WRONLY | O_APPEND | O_CREAT, 0640)) < 0)
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

void
map_log(void)
{
	struct stat meta;
	int fd;

	if ((fd = open("smak/log", O_RDONLY)) < 0)
		die("cannot open central log:");
	if (fstat(fd, &meta) < 0)
		die("cannot stat central log:");

	log_base = mmap(NULL, meta.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (log_base == MAP_FAILED)
		die("mmap():");
	log_length = meta.st_size;

	close(fd);
}

void
unmap_log(void)
{
	munmap(log_base, log_length);
}

static void *
aether_alloc(size_t size)
{
	void *ptr = aether_cursor;
	if (size > MAX_AETHER_MEMORY - (aether_cursor - aether_base))
		die("not enough aether memory.");
	aether_cursor += size;
	return ptr;
}

void
read_from_log(MSG msg, const char *info[])
{
	char *cursor = log_base + msg;
	char *end, *buf;
	int i;
	for (i = 0; i < MNUMINFO; i++) {
		end = memchr(cursor,
			i == MNUMINFO-1 ? '\n' : '\t',
			log_length - (cursor - log_base));
		if (!end)
			die("central log file is corrupt.");
		buf = aether_alloc(end - cursor + 1);
		memcpy(buf, cursor, end - cursor);
		buf[end - cursor] = '\0';
		info[i] = buf;
		cursor = end + 1;
	}
}

void
read_report(struct report *rpt, int year, int month)
{
	char filename[100];
	struct stat meta;
	rpt->year = year;
	rpt->month = month;
	snprintf(filename, sizeof filename,
		"smak/report/%04d-%02d", year, month);
	if ((rpt->fd = open(filename, O_RDWR | O_CREAT, 0640)) < 0)
		die("open():");
	if (fstat(rpt->fd, &meta) < 0)
		die("fstat():");
	if (!(rpt->entries = malloc(meta.st_size)))
		die("malloc():");
	check_read(rpt->fd, rpt->entries, meta.st_size);
	rpt->count = meta.st_size / sizeof *rpt->entries;
}

void
write_report(const struct report *rpt)
{
	lseek(rpt->fd, 0, SEEK_SET);
	check_write(rpt->fd, rpt->entries, rpt->count * sizeof *rpt->entries);
}

void
close_report(struct report *rpt)
{
	free(rpt->entries);
	close(rpt->fd);
}

void
add_to_report(struct report *rpt, time_t time, MSG msg)
{
	size_t idx;

	if (!(rpt->entries = realloc(rpt->entries, (rpt->count + 1) * sizeof *rpt->entries)))
		die("realloc():");

	for (idx = 0; idx < rpt->count; idx++) {
		if (rpt->entries[idx].time > time) break;
	}
	memmove(&rpt->entries[idx + 1], &rpt->entries[idx],
		(rpt->count - idx) * sizeof *rpt->entries);
	rpt->entries[idx] = (struct repent) { time, msg };
	rpt->count++;
}
 
