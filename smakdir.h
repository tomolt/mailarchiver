/* See LICENSE file for copyright and license details. */

#include <stddef.h>

enum {
	MUNIQ,
	MMSGID,
	MSUBJECT,
	MFROM,
	MINREPLYTO,
	MTIME,
	MNUMINFO
};

typedef size_t MSG;

/* entry in a report */
struct repent {
	time_t time;
	MSG    msg;
};

/* monthly report page */
struct report {
	int year;
	int month;
	int fd;
	size_t count;
	struct repent *entries;
};

void init_smakdir(void);
size_t add_to_log(const char *info[]);

void map_log(void);
void unmap_log(void);
void read_from_log(MSG msg, const char *info[]);

void read_report  (struct report *rpt, int year, int month);
void write_report (const struct report *rpt);
void close_report (struct report *rpt);
void add_to_report(struct report *rpt, time_t time, MSG msg);

