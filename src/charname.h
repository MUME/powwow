/* look up characters by their Unicode names */

#ifndef _CHARNAME_H_
#define _CHARNAME_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the file with the names, made by doc/unicode-names.sh */
extern char namesfile[];

bool charname_lookup(const char *name, size_t len, uint32_t *cp);
int charname_complete(const char *prefix, int n, char *name, char *common,
                      size_t size);

#endif /* _CHARNAME_H_ */
