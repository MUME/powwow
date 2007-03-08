/*
 *  ptr.h -- type definitions for ptr (aka "pointer"), a char* replacement
 *           which allows for '\0' inside a string.
 */

#ifndef _PTR_H_
#define _PTR_H_

typedef struct s_ptr {
    int len;
    int max;
    int signature;
} _ptr;

typedef _ptr * ptr;

#define sizeofptr ((int)(1 + sizeof(_ptr)))

/* the + 0 below is to prohibit using the macros for altering the ptr */
#define ptrlen(p) ((p)->len + 0)
#define ptrmax(p) ((p)->max + 0)
#define ptrdata(p) ((char *)((ptr)(p) + 1))
/* if p is a valid (ptr), ptrdata(p) is guaranteed to be a valid (char *) */

ptr   ptrnew    __P ((int max));
ptr   ptrdup2   __P ((ptr src, int newmax));
ptr   ptrdup    __P ((ptr src));

#define PTR_SIG 91887
#define ptrdel(x) _ptrdel(x);x=(ptr)0;
void  _ptrdel    __P ((ptr p));

void  ptrzero   __P ((ptr p));
void  ptrshrink __P ((ptr p, int len));
void  ptrtrunc  __P ((ptr p, int len));
ptr   ptrpad    __P ((ptr p, int len));
ptr   ptrsetlen __P ((ptr p, int len));

ptr   ptrcpy    __P ((ptr dst, ptr src));
ptr   ptrmcpy   __P ((ptr dst, char *src, int len));

ptr   ptrcat    __P ((ptr dst, ptr src));
ptr   ptrmcat   __P ((ptr dst, char *src, int len));


ptr __ptrcat    __P ((ptr dst, char *src, int len, int shrink));
ptr __ptrmcpy   __P ((ptr dst, char *src, int len, int shrink));

int   ptrcmp    __P ((ptr p, ptr q));
int   ptrmcmp   __P ((ptr p, char *q, int lenq));

char *ptrchr    __P ((ptr p, char c));
char *ptrrchr   __P ((ptr p, char c));

char *ptrfind    __P ((ptr p, ptr q));
char *ptrmfind   __P ((ptr p, char *q, int lenq));

char *ptrchrs   __P ((ptr p, ptr q));
char *ptrmchrs  __P ((ptr p, char *q, int lenq));
char *ptrrchrs  __P ((ptr p, ptr q));
char *ptrmrchrs __P ((ptr p, char *q, int lenq));

char *memchrs   __P ((char *p, int lenp, char *q, int lenq));
char *memrchrs  __P ((char *p, int lenp, char *q, int lenq));
#ifdef _GNU_SOURCE
# define memfind memmem
#else
char *memfind   __P ((char *hay, int haylen, char *needle, int needlelen));
/* TODO: watch memrchr, it is defined differently here than under _GNU_SOURCE,
 * so it could cause bizarre results if a module makes use of a library that
 * uses it */
char *memrchr   __P ((char *p, int lenp, char c));
#endif

#endif /* _PTR_H_ */
