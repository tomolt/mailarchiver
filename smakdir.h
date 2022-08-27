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

size_t add_to_log(const char *info[]);

