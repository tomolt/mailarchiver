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

void init_smakdir(void);
size_t add_to_log(const char *info[]);

void map_log(void);
void unmap_log(void);
void read_from_log(MSG msg, const char *info[]);

