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

void init_smakdir(void);
size_t add_to_log(const char *info[]);

