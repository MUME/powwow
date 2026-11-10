/* public things from map.c */

#ifndef _MAP_H_
#define _MAP_H_

#include <stdbool.h>

void map_bootstrap(void);

void map_retrace(int steps, int walk_back);
void map_show(void);
void map_sprintf(char *buf);
void map_add_dir(char dir);
bool map_walk(char *word, bool silent, bool maponly);

#endif /* _MAP_H_ */

