/* public things from map.c */

#ifndef _MAP_H_
#define _MAP_H_

void map_bootstrap(void);

void map_retrace(int steps, int walk_back);
void map_show(void);
void map_sprintf(char *buf);
void map_add_dir(char dir);
int  map_walk(char *word, int silent, int maponly);

#endif /* _MAP_H_ */

