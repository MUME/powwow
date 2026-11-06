/*
 *  ptr.h -- type definitions for ptr (aka "pointer"), a char* replacement
 *           which allows for '\0' inside a string.
 */

#ifndef PTR_H
#define PTR_H

typedef struct
{
    int len;			/* chars in data, excluding the final '\0' */
    int max;			/* capacity of data, excluding the final '\0' */
    int signature;		/* PTR_SIG, if allocated by ptrnew() etc.;
				   ptrdel() does not free it otherwise */
    char data[];		/* the contents, which may contain '\0';
				   data[len] is always '\0' */
} ptr;

static inline int ptrlen(const ptr *p)
{
    return p->len;
}

static inline int ptrmax(const ptr *p)
{
    return p->max;
}

/*
 * if p is a valid (ptr *), ptrdata(p) is guaranteed to be a valid (char *);
 * like strchr(), this returns a modifiable pointer for a const argument
 */
static inline char *ptrdata(const ptr *p)
{
    return (char *)p->data;
}

ptr  *ptrnew(int max);
ptr  *ptrdup2(const ptr *src, int newmax);
ptr  *ptrdup(const ptr *src);

/* free *p, if it is a ptr, and set *p to NULL */
void  ptrdel(ptr **p);

void  ptrzero(ptr *p);
void  ptrshrink(ptr *p, int len);
void  ptrtrunc(ptr *p, int len);
ptr  *ptrpad(ptr *p, int len);
ptr  *ptrsetlen(ptr *p, int len);

ptr  *ptrcpy(ptr *dst, const ptr *src);
ptr  *ptrmcpy(ptr *dst, const char *src, int len);

ptr  *ptrcat(ptr *dst, const ptr *src);
ptr  *ptrmcat(ptr *dst, const char *src, int len);

int   ptrcmp(const ptr *p, const ptr *q);
int   ptrmcmp(const ptr *p, const char *q, int lenq);

/*
 * like strchr(), the functions below return modifiable pointers
 * into their const arguments
 */
char *ptrchr(const ptr *p, char c);
char *ptrrchr(const ptr *p, char c);

char *ptrfind(const ptr *p, const ptr *q);
char *ptrmfind(const ptr *p, const char *q, int lenq);

char *ptrchrs(const ptr *p, const ptr *q);
char *ptrmchrs(const ptr *p, const char *q, int lenq);
char *ptrrchrs(const ptr *p, const ptr *q);
char *ptrmrchrs(const ptr *p, const char *q, int lenq);

char *memchrs(const char *p, int lenp, const char *q, int lenq);
char *memrchrs(const char *p, int lenp, const char *q, int lenq);
char *memfind(const char *hay, int haylen, const char *needle, int needlelen);

#endif /* PTR_H */
