/*
 *  ptr.c  --  basic data structures and functions to manipulate them
 *
 *  Copyright (C) 1998 by Massimiliano Ghilardi
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <limits.h>
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
#include "ptr.h"

enum {
    /* the header, and the terminating '\0' */
    PTR_OVERHEAD = sizeof(ptr) + 1,
    /* the largest max that can be allocated */
    PTR_MAX = INT_MAX - PTR_OVERHEAD,
};

/*
 * a ptr longer than #setvar mem, or than one can be, sets error to
 * MEM_LIMIT_ERROR, and the function gives up; running out of memory ends
 * powwow
 */

/*
 * create a new, empty ptr.
 * return NULL if max == 0
 */
ptr *ptrnew(int max)
{
    if (max == 0)
        return NULL;
    if (max < 0 || max > PTR_MAX || (limit_mem != 0 && max > limit_mem)) {
        error = MEM_LIMIT_ERROR;
        return NULL;
    }
    ptr *p = xmalloc(max + PTR_OVERHEAD);
    p->max = max;
    p->len = 0;
    ptrdata(p)[0] = '\0';
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
    if (newmax == 0)
        return NULL;
    if (newmax < 0 || newmax > PTR_MAX
        || (limit_mem != 0 && newmax > limit_mem)) {
        error = MEM_LIMIT_ERROR;
        return NULL;
    }
    if (src == NULL)
        return ptrnew(newmax);
    ptr *p = xmalloc(newmax + PTR_OVERHEAD);
    p->max = newmax;
    if (newmax > ptrlen(src))
        newmax = ptrlen(src);
    p->len = newmax;
    memmove(ptrdata(p), ptrdata(src), newmax);
    ptrdata(p)[newmax] = '\0';
    return p;
}

ptr *ptrdup(const ptr *src)
{
    if (src == NULL)
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
    if (p != NULL) {
        p->len = 0;
        ptrdata(p)[0] = '\0';
    }
}

/* truncate a ptr to len chars */
void ptrtrunc(ptr *p, int len)
{
    if (p != NULL) {
        if (len < 0 || len > ptrlen(p))
            return;
        p->len = len;
        ptrdata(p)[len] = '\0';
    }
}

/* shrink a ptr by len chars */
void ptrshrink(ptr *p, int len)
{
    if (p != NULL) {
        if (len < 0 || len > ptrlen(p))
            return;
        p->len -= len;
        ptrdata(p)[p->len] = '\0';
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
    if (src == NULL || len <= 0)
        return dst;
    if (len > PTR_MAX - (dst != NULL ? ptrlen(dst) : 0)) {
        error = MEM_LIMIT_ERROR;
        return dst;
    }

    int failmax;
    bool mustalloc;
    if (dst == NULL) {
        failmax = len;
        mustalloc = true;
    } else {
        failmax = ptrlen(dst) + len;
        mustalloc = ptrmax(dst) < ptrlen(dst) + len;

        if (ptrmax(dst) > PARAMLEN && ptrmax(dst) / 4 > ptrlen(dst) + len) {
            /* we're wasting space, shrink dst */
            mustalloc = true;
        }
    }

    if (mustalloc) {
        /* dst must be (re)allocated */

        /* ugly but working: check for overlapping dst and src */
        bool overlap = (dst != NULL && src >= ptrdata(dst)
                        && src < ptrdata(dst) + ptrmax(dst));

        /* find a suitable new size */
        if (limit_mem != 0 && failmax > limit_mem) {
            error = MEM_LIMIT_ERROR;
            return dst;
        }
        int newmax;
        if (failmax < PARAMLEN / 2)
            newmax = PARAMLEN;
        else if (failmax / 1024 < PARAMLEN && failmax <= PTR_MAX - PARAMLEN)
            newmax = failmax + PARAMLEN;
        else
            newmax = failmax;
        if (limit_mem != 0 && newmax > limit_mem) {
            if (len + (dst != NULL ? ptrlen(dst) : 0) > limit_mem)
                len = limit_mem - (dst != NULL ? ptrlen(dst) : 0);
            if (len < 0)
                len = 0;
            newmax = limit_mem;
        }
        if (overlap && newmax < ptrmax(dst)) {
            /* src may not survive shrinking dst */
            memmove(ptrdata(dst) + ptrlen(dst), src, len);
            src = ptrdata(dst) + ptrlen(dst);
        }
        const size_t src_offset = overlap ? src - ptrdata(dst) : 0;
        ptr *p = xrealloc(dst, newmax + PTR_OVERHEAD);
        if (dst == NULL)
            p->len = 0;
        p->max = newmax;
        if (overlap)
            src = ptrdata(p) + src_offset;
        dst = p;
    }
    if (ptrdata(dst) + ptrlen(dst) != src)
        memmove(ptrdata(dst) + ptrlen(dst), src, len);
    dst->len += len;
    ptrdata(dst)[ptrlen(dst)] = '\0';
    return dst;
}

ptr *ptrcat(ptr *dst, const ptr *src)
{
    if (src != NULL)
        return ptrmcat(dst, ptrdata(src), ptrlen(src));
    return dst;
}

/*
 * copy a ptr into another (ptrcpy), or a char* into a ptr (ptrmcpy);
 * same warning as above if dst is too small or way too big.
 */
ptr *ptrmcpy(ptr *dst, const char *src, int len)
{
    if (src == NULL || len <= 0) {
        if (len >= 0)
            ptrzero(dst);
        return dst;
    }
    if (len > PTR_MAX) {
        error = MEM_LIMIT_ERROR;
        return dst;
    }

    bool mustalloc;
    if (dst == NULL) {
        mustalloc = true;
    } else {
        mustalloc = ptrmax(dst) < len;

        if (ptrmax(dst) > PARAMLEN && ptrmax(dst) / 4 > len) {
            /* we're wasting space, shrink dst */
            mustalloc = true;
        }
    }

    if (mustalloc) {
        /* dst must be (re)allocated */

        /* ugly but working: check for overlapping dst and src */
        bool overlap = (dst != NULL && src >= ptrdata(dst)
                        && src < ptrdata(dst) + ptrmax(dst));

        /* find a suitable new size */
        int failmax = len;
        if (limit_mem != 0 && failmax > limit_mem) {
            error = MEM_LIMIT_ERROR;
            return dst;
        }
        int newmax;
        if (failmax < PARAMLEN / 2)
            newmax = PARAMLEN;
        else if (failmax / 1024 < PARAMLEN && failmax <= PTR_MAX - PARAMLEN)
            newmax = failmax + PARAMLEN;
        else
            newmax = failmax;
        if (limit_mem != 0 && newmax > limit_mem) {
            if (len > limit_mem)
                len = limit_mem;
            newmax = limit_mem;
        }

        if (overlap && newmax < ptrmax(dst)) {
            /* src may not survive shrinking dst */
            memmove(ptrdata(dst), src, len);
            src = ptrdata(dst);
        }
        const size_t src_offset = overlap ? src - ptrdata(dst) : 0;
        ptr *p = xrealloc(dst, newmax + PTR_OVERHEAD);
        if (dst == NULL)
            p->len = 0;
        p->max = newmax;
        if (overlap)
            src = ptrdata(p) + src_offset;
        dst = p;
    }
    if (ptrdata(dst) != src) {
        /* src was moved along with dst above if it pointed inside dst */
        /* NOLINTNEXTLINE(clang-analyzer-unix.Malloc) */
        memmove(ptrdata(dst), src, len);
    }
    dst->len = len;
    ptrdata(dst)[ptrlen(dst)] = '\0';
    return dst;
}

ptr *ptrcpy(ptr *dst, const ptr *src)
{
    if (src != NULL)
        return ptrmcpy(dst, ptrdata(src), ptrlen(src));
    ptrzero(dst);
    return dst;
}

/* enlarge a ptr by len chars. create new if needed */
ptr *ptrpad(ptr *p, int len)
{
    if (p == NULL) {
        if (len <= 0)
            return NULL;
        return ptrnew(len);
    }
    if (len < -ptrlen(p))
        len = -ptrlen(p);
    if (len > ptrmax(p) - ptrlen(p)) {
        /* must realloc the ptr */
        if (len > PTR_MAX - ptrlen(p)) {
            error = MEM_LIMIT_ERROR;
            return p;
        }
        len += ptrlen(p);
        /*
         * cheat: we use ptrmcpy with src==dst
         * and do an out-of-bound read of src.
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
    if (p == NULL) {
        if (len <= 0)
            return NULL;
        p = ptrnew(len);
        if (p != NULL) {
            p->len = len;
            ptrdata(p)[len] = '\0';
        }
        return p;
    }
    return ptrpad(p, (len > 0 ? len : 0) - ptrlen(p));
}

/*
 * compare two ptr (ptrcmp) or a ptr and a char* (ptrmcmp)
 * if one is a truncated copy of the other, the shorter is considered smaller
 */
int ptrmcmp(const ptr *p, const char *q, int lenq)
{
    if (p == NULL || ptrlen(p) == 0) {
        if (q == NULL || lenq <= 0) {
            /* both empty */
            return 0;
        }
        return -1;
    }
    if (q == NULL || lenq <= 0)
        return 1;
    int res = memcmp(ptrdata(p), q, MIN2(ptrlen(p), lenq));
    if (res != 0)
        return res;
    if (ptrlen(p) < lenq)
        return -1;
    if (ptrlen(p) > lenq)
        return 1;
    return 0;
}

int ptrcmp(const ptr *p, const ptr *q)
{
    if (q != NULL)
        return ptrmcmp(p, ptrdata(q), ptrlen(q));
    if (p != NULL)
        return 1;
    return 0;
}

/*
 * find first occurrence of c in p
 * return NULL if none found.
 */
char *ptrchr(const ptr *p, char c)
{
    if (p != NULL)
        return memchr(ptrdata(p), c, ptrlen(p));
    return NULL;
}

/*
 * find last occurrence of c in p
 * return NULL if none found.
 */
char *ptrrchr(const ptr *p, char c)
{
    if (p != NULL) {
        for (int i = ptrlen(p) - 1; i >= 0; --i) {
            if (ptrdata(p)[i] == c)
                return ptrdata(p) + i;
        }
    }
    return NULL;
}

/*
 * find first occurrence of needle in hay
 */
char *(memfind)(const char *hay, int haylen, const char *needle, int needlelen)
{
    if (hay == NULL || haylen <= 0 || needlelen < 0)
        return NULL;
    if (needle == NULL || needlelen == 0)
        return (char *)hay;

    while (haylen >= needlelen) {
        /* find a matching first char */
        const char *tmp = memchr(hay, *needle, haylen);
        if (tmp == NULL)
            return NULL;
        haylen -= tmp - hay;
        if (haylen < needlelen)
            return NULL;
        hay = tmp;

        /* got a matching first char, check the rest */
        if (memcmp(needle, tmp, needlelen) == 0)
            return (char *)tmp;

        ++hay;
        --haylen;
    }

    return NULL;
}

/*
 * find first occurrence of q in p,
 * return NULL if none found.
 */
char *ptrmfind(const ptr *p, const char *q, int lenq)
{
    if (p != NULL) {
        if (q != NULL && lenq > 0)
            return memfind(ptrdata(p), ptrlen(p), q, lenq);
        return ptrdata(p);
    }
    return NULL;
}

char *ptrfind(const ptr *p, const ptr *q)
{
    if (p != NULL) {
        if (q != NULL)
            return memfind(ptrdata(p), ptrlen(p), ptrdata(q), ptrlen(q));
        return ptrdata(p);
    }
    return NULL;
}

/*
 * Scan p for the first occurrence of one of the characters in q,
 * return NULL if none of them is found.
 */
char *(memchrs)(const char *p, int lenp, const char *q, int lenq)
{
    if (q == NULL || lenq <= 0)
        return (char *)p;
    if (p == NULL || lenp <= 0)
        return NULL;

    const char *endp = p + lenp;

    while (p < endp && memchr(q, *p, lenq) == NULL)
        ++p;

    if (p == endp)
        return NULL;
    return (char *)p;
}

char *ptrmchrs(const ptr *p, const char *q, int lenq)
{
    if (p != NULL)
        return memchrs(ptrdata(p), ptrlen(p), q, lenq);
    return NULL;
}

char *ptrchrs(const ptr *p, const ptr *q)
{
    if (p != NULL) {
        if (q != NULL)
            return memchrs(ptrdata(p), ptrlen(p), ptrdata(q), ptrlen(q));
        return ptrdata(p);
    }
    return NULL;
}

/*
 * Scan p for the last occurrence of one of the characters in q,
 * return NULL if none of them is found.
 */
char *(memrchrs)(const char *p, int lenp, const char *q, int lenq)
{
    if (p == NULL || lenp <= 0) {
        if (q == NULL || lenq <= 0)
            return (char *)p;
        return NULL;
    }

    p += lenp;
    if (q == NULL || lenq <= 0)
        return (char *)p;
    do {
        --lenp;
        --p;
    } while (lenp >= 0 && memchr(q, *p, lenq) == NULL);

    if (lenp < 0)
        return NULL;
    return (char *)p;
}

char *ptrmrchrs(const ptr *p, const char *q, int lenq)
{
    if (p != NULL)
        return memrchrs(ptrdata(p), ptrlen(p), q, lenq);
    return NULL;
}

char *ptrrchrs(const ptr *p, const ptr *q)
{
    if (p != NULL && q != NULL)
        return memrchrs(ptrdata(p), ptrlen(p), ptrdata(q), ptrlen(q));
    return p != NULL ? ptrdata(p) + ptrlen(p) : NULL;
}
