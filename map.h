/* public things from map.c */

#ifndef _MAP_H_
#define _MAP_H_

void map_bootstrap	__P ((void));

void map_retrace	__P ((int steps, int walk_back));
void map_show		__P ((void));
void map_sprintf	__P ((char *buf));
void map_add_dir	__P ((char dir));
int  map_walk		__P ((char *word, int silent, int maponly));

#endif /* _MAP_H_ */

