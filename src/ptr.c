/*
 *  data.c  --  basic data structures and functions to manipulate them
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include "defines.h"
#include "main.h"
#include "utils.h"
#include "eval.h"

enum
{
    /* the header, and the terminating '\0' */
    PTR_OVERHEAD = sizeof(ptr) + 1,
};

/*
 * create a new, empty ptr.
 * return NULL if max == 0
 */
ptr *ptrnew(int max)
{
    ptr *p = (ptr *)0;

    if (max == 0)
	;
    else if (limit_mem && max > limit_mem)
	error = MEM_LIMIT_ERROR;
    else if (max < 0 || max + PTR_OVERHEAD < max) /* overflow! */
	error = NO_MEM_ERROR;
    else if ((p = (ptr *)malloc(max + PTR_OVERHEAD))) {
	p->max = max;
	ptrdata(p)[p->len = 0] = '\0';
    } else
	error = NO_MEM_ERROR;
    return p;
}

/*
 * create a new ptr giving an initial contents,
 * which gets duplicated.
 *
 * warning: newmax could be so small that we must truncate the copied data!
 */
ptr *ptrdup2(const ptr *src, int newmax)
{
    ptr *p = (ptr *)0;

    if (newmax == 0)
	;
    else if (newmax < 0 || newmax + PTR_OVERHEAD < newmax)
	error = NO_MEM_ERROR;
    else if (limit_mem && newmax > limit_mem)
	error = MEM_LIMIT_ERROR;
    else if (!src)
	p = ptrnew(newmax);
    else if ((p = malloc(newmax + PTR_OVERHEAD))) {
	p->max = newmax;
	if (newmax > ptrlen(src))
	    newmax = ptrlen(src);
	memmove(ptrdata(p), ptrdata(src), p->len = newmax);
	ptrdata(p)[newmax] = '\0';
    } else
	error = NO_MEM_ERROR;
    return p;
}

ptr *ptrdup(const ptr *src)
{
    if (!src)
	return NULL;
    return ptrdup2(src, ptrlen(src));
}

/* delete (free) a ptr, and set it to NULL */
void ptrdel(ptr **p)
{
    free(*p);
    *p = NULL;
}

/* clear a ptr */
void ptrzero(ptr *p)
{
    if (p) {
	p->len = 0;
	ptrdata(p)[0] = '\0';
    }
}

/* truncate a ptr to len chars */
void ptrtrunc(ptr *p, int len)
{
    if (p) {
	if (len < 0 || len > ptrlen(p))
	    return;
	ptrdata(p)[p->len = len] = '\0';
    }
}

/* shrink a ptr by len chars */
void ptrshrink(ptr *p, int len)
{
    if (p) {
	if (len < 0 || len > ptrlen(p))
	    return;
	ptrdata(p)[p->len -= len] = '\0';
    }
}

/*
 * concatenate two ptr (ptrcat) or a ptr and a char* (ptrmcat)
 * result may be a _newly_allocated_ ptr if original one
 * is too small or if it is soooo big that we are wasting tons of memory.
 * In both cases, the original one may get deleted (freed)
 * You have been warned! Don't use any statically created ptr for
 * write operations, and you will be fine.
 */
ptr *ptrmcat(ptr *dst, const char *src, int len)
{
    int newmax, failmax, overlap;
    char mustalloc;

    if (!src || len <= 0)
	return dst;
    if (len + PTR_OVERHEAD < 0) {
	/* overflow! */
	error = NO_MEM_ERROR;
	return dst;
    }

    if (!dst) {
	failmax = len;
	mustalloc = 1;
    } else {
	failmax = ptrlen(dst) + len;
	mustalloc = ptrmax(dst) < ptrlen(dst) + len;

	if (ptrmax(dst) > PARAMLEN && ptrmax(dst)/4 > ptrlen(dst) + len)
	    /* we're wasting space, shrink dst */
	    mustalloc = 1;
    }

    if (failmax + PTR_OVERHEAD < 0)  {
	/* overflow! */
	error = NO_MEM_ERROR;
	return dst;
    }

    if (mustalloc) {
	/* dst must be (re)allocated */
	ptr *p;

	/* ugly but working: check for overlapping dst and src */
	if (dst && src >= ptrdata(dst) && src < ptrdata(dst) + ptrmax(dst))
	    overlap = 1;
	else
	    overlap = 0;

	/* find a suitable new size */
	if (limit_mem && failmax > limit_mem) {
	    error = MEM_LIMIT_ERROR;
	    return dst;
	}
	if (failmax < PARAMLEN / 2)
	    newmax = PARAMLEN;
	else if (failmax / 1024 < PARAMLEN && failmax + PARAMLEN + PTR_OVERHEAD > 0)
	    newmax = failmax + PARAMLEN;
	else
	    newmax = failmax;
	if (limit_mem && newmax > limit_mem) {
	    if (len + (dst ? ptrlen(dst) : 0) > limit_mem)
		len = limit_mem - (dst ? ptrlen(dst) : 0);
	    if (len < 0)
		len = 0;
	    newmax = limit_mem;
	}
	if ((p = (ptr *)realloc((void *)dst, newmax + PTR_OVERHEAD))) {
	    if (overlap)
	        src = ptrdata(p) + (src - ptrdata(dst));
	    if (!dst)
		p->len = 0;
	    p->max = newmax;
	    dst = p;
	} else if ((p = ptrdup2(dst, newmax))) {
	    if (overlap)
	        src = ptrdata(p) + (src - ptrdata(dst));
	    ptrdel(&dst);
	    dst = p;
	} else {
	    error = NO_MEM_ERROR;
	    return dst;
	}
    }
    if (ptrdata(dst) + ptrlen(dst) != src)
	memmove(ptrdata(dst) + ptrlen(dst), src, len);
    dst->len += len;
    ptrdata(dst)[ptrlen(dst)] = '\0';
    return dst;
}

ptr *ptrcat(ptr *dst, const ptr *src)
{
    if (src)
	return ptrmcat(dst, ptrdata(src), ptrlen(src));
    return dst;
}

/*
 * copy a ptr into another (ptrcpy), or a char* into a ptr (ptrmcpy);
 * same warning as above if dst is too small or way too big.
 */
ptr *ptrmcpy(ptr *dst, const char *src, int len)
{
    int newmax, failmax = len, overlap;
    char mustalloc;

    if (!src || len<=0) {
	if (len>=0)
	    ptrzero(dst);
	return dst;
    }
    if (failmax + PTR_OVERHEAD < 0)  {
	/* overflow! */
	error = NO_MEM_ERROR;
	return dst;
    }

    if (!dst) {
	mustalloc = 1;
    } else {
	mustalloc = ptrmax(dst) < len;

	if (ptrmax(dst) > PARAMLEN && ptrmax(dst)/4 > len)
	    /* we're wasting space, shrink dst */
	    mustalloc = 1;
    }

    if (mustalloc) {
	/* dst must be (re)allocated */
	ptr *p;

	/* ugly but working: check for overlapping dst and src */
	if (dst && src >= ptrdata(dst) && src < ptrdata(dst) + ptrmax(dst))
	    overlap = 1;
	else
	    overlap = 0;

	/* find a suitable new size */
	if (limit_mem && failmax > limit_mem) {
	    error = MEM_LIMIT_ERROR;
	    return dst;
	}
	if (failmax < PARAMLEN / 2)
	    newmax = PARAMLEN;
	else if (failmax / 1024 < PARAMLEN && failmax + PARAMLEN + PTR_OVERHEAD > 0)
	    newmax = failmax + PARAMLEN;
	else
	    newmax = failmax;
	if (limit_mem && newmax > limit_mem) {
	    if (len > limit_mem)
		len = limit_mem;
	    newmax = limit_mem;
	}

	if ((p = (ptr *)realloc((void *)dst, newmax + PTR_OVERHEAD))) {
	    if (overlap)
	        src = ptrdata(p) + (src - ptrdata(dst));
	    if (!dst)
		p->len = 0;
	    p->max = newmax;
	    dst = p;
	} else if ((p = ptrdup2(dst, newmax))) {
	    if (overlap)
	        src = ptrdata(p) + (src - ptrdata(dst));
	    ptrdel(&dst);
	    dst = p;
	} else {
	    error = NO_MEM_ERROR;
	    return dst;
	}
    }
    if (ptrdata(dst) != src)
	memmove(ptrdata(dst), src, len);
    dst->len = len;
    ptrdata(dst)[ptrlen(dst)] = '\0';
    return dst;
}

ptr *ptrcpy(ptr *dst, const ptr *src)
{
    if (src)
	return ptrmcpy(dst, ptrdata(src), ptrlen(src));
    ptrzero(dst);
    return dst;
}

/* enlarge a ptr by len chars. create new if needed */
ptr *ptrpad(ptr *p, int len)
{
    if (!p) {
	if (len<=0)
	    return p;
	else
	    return ptrnew(len);
    }
    if (len > ptrmax(p) - ptrlen(p)) {
	/* must realloc the ptr */
	len += ptrlen(p);
	if (len < 0)  {
	    /* overflow! */
	    error = NO_MEM_ERROR;
	    return p;
	}
	/*
	 * cheat: we use ptrmcpy with src==dst
	 * and do an out-of-boud read of src.
	 * works since dst (==src) gets enlarged
	 * before doing the copy.
	 */
	p = ptrmcpy(p, ptrdata(p), len);
    } else {
	p->len += len;
	ptrdata(p)[ptrlen(p)] = '\0';
    }
    return p;
}

/* set a ptr to be len chars at minimum. create new if needed */
ptr *ptrsetlen(ptr *p, int len)
{
    if (!p) {
	if (len<=0)
	    return p;
	else {
	    if ((p = ptrnew(len)))
		ptrdata(p)[p->len = len] = '\0';
	    return p;
	}
    }
    return ptrpad(p, len - ptrlen(p));
}

/*
 * compare two ptr (ptrcmp) or a ptr and a char* (ptrmcmp)
 * if one is a truncated copy of the other, the shorter is considered smaller
 */
int ptrmcmp(const ptr *p, const char *q, int lenq)
{
    int res;
    if (!p || !ptrlen(p)) {
	if (!q || lenq<=0)
	    /* both empty */
	    res = 0;
    	else
	    res = -1;
    } else if (!q || lenq<=0)
	res = 1;
    else if ((res = memcmp(ptrdata(p), q, MIN2(ptrlen(p), lenq))))
	;
    else if (ptrlen(p) < lenq)
	res = -1;
    else if (ptrlen(p) > lenq)
	res = 1;
    else
	res = 0;
    return res;
}

int ptrcmp(const ptr *p, const ptr *q)
{
    if (q)
	return ptrmcmp(p, ptrdata(q), ptrlen(q));
    else if (p)
	return 1;
    else
	return 0;
}

/*
 * find first occurrence of c in p
 * return NULL if none found.
 */
char *ptrchr(const ptr *p, char c)
{
    if (p)
	return (char *)memchr(ptrdata(p), c, ptrlen(p));
    return (char*)p; /* shortcut for NULL */
}

/*
 * find last occurrence of c in p
 * return NULL if none found.
 */
char *ptrrchr(const ptr *p, char c)
{
    if (p != NULL) {
        for (int i = ptrlen(p) - 1; i >= 0; --i)
            if (ptrdata(p)[i] == c)
                return ptrdata(p) + i;
        return NULL;
    }
    return (char*)p; /* shortcut for NULL */
}

/*
 * find first occurrence of needle in hay
 */
char *memfind(const char *hay, int haylen, const char *needle, int needlelen)
{
    const char *tmp;

    if (!hay || haylen<=0 || needlelen<0)
	return NULL;
    if (!needle || !needlelen)
	return (char *)hay;

    while (haylen >= needlelen) {
	/* find a matching first char */
	if ((tmp = memchr(hay, *needle, haylen))) {
	    if ((haylen -= (tmp-hay)) < needlelen)
		return NULL;
	    hay = tmp;
	} else
	    return NULL;

	/* got a matching first char,
	 * check the rest */
	if (!memcmp(needle, tmp, needlelen))
	    return (char *)tmp;

	hay++, haylen --;
    }

    return NULL;
}

/*
 * find first occurrence of q in p,
 * return NULL if none found.
 */
char *ptrmfind(const ptr *p, const char *q, int lenq)
{
    if (p) {
	if (q && lenq>0)
	    return (char *)memfind(ptrdata(p), ptrlen(p), q, lenq);
	return ptrdata(p);
    }
    return (char*)p; /* shortcut for NULL */
}

char *ptrfind(const ptr *p, const ptr *q)
{
    if (p) {
	if (q)
	    return (char *)memfind(ptrdata(p), ptrlen(p), ptrdata(q), ptrlen(q));
	return ptrdata(p);
    }
    return (char*)p; /* shortcut for NULL */
}


/*
 * Scan p for the first occurrence of one of the characters in q,
 * return NULL if none of them is found.
 */
char *memchrs(const char *p, int lenp, const char *q, int lenq)
{
    const char *endp;

    if (!q || lenq<=0)
	return (char *)p;
    if (!p || lenp<=0)
	return NULL;

    endp = p + lenp;

    while (p < endp && !memchr(q, *p, lenq))
	p++;

    if (p == endp)
	return NULL;
    return (char *)p;
}

char *ptrmchrs(const ptr *p, const char *q, int lenq)
{
    if (p)
	return memchrs(ptrdata(p), ptrlen(p), q, lenq);
    return (char*)p; /* shortcut for NULL */
}

char *ptrchrs(const ptr *p, const ptr *q)
{
    if (p) {
	if (q)
	    return memchrs(ptrdata(p), ptrlen(p), ptrdata(q), ptrlen(q));
	return ptrdata(p);
    }
    return (char*)p; /* shortcut for NULL */
}


/*
 * Scan p for the last occurrence of one of the characters in q,
 * return NULL if none of them is found.
 */
char *memrchrs(const char *p, int lenp, const char *q, int lenq)
{
    if (!p || lenp<=0) {
	if (!q || lenq<=0)
	    return (char *)p;
	else
	    return NULL;
    }

    p += lenp;
    if (!q || lenq<=0)
	return (char *)p;
    do {
	lenp--, p--;
    } while (lenp >= 0 && !memchr(q, *p, lenq));

    if (lenp < 0)
	return NULL;
    return (char *)p;
}

char *ptrmrchrs(const ptr *p, const char *q, int lenq)
{
    if (p)
	return memrchrs(ptrdata(p), ptrlen(p), q, lenq);
    return (char*)p; /* shortcut for NULL */
}

char *ptrrchrs(const ptr *p, const ptr *q)
{
    if (p && q)
	return memrchrs(ptrdata(p), ptrlen(p), ptrdata(q), ptrlen(q));
    return p ? ptrdata(p) + ptrlen(p) : (char*)p; /* shortcut for NULL */
}

