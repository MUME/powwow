/*
 *  test_ptr.c  --  unit tests for ptr.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "defines.h"
#include "main.h"
#include "eval.h"

/* needed by ptr.c */
int error;
int limit_mem;

static int failures;

static void check(const char *what, bool ok)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        ++failures;
    }
}

static bool has_contents(const ptr *p, const char *expect)
{
    size_t len = strlen(expect);
    return (p != NULL
	    && (size_t)ptrlen(p) == len
	    && memcmp(ptrdata(p), expect, len + 1) == 0);
}

/* return a ptr containing 'len' chars 'a' to 'z', in a buffer of size 'max' */
static ptr *new_alphabet(int max, int len)
{
    ptr *p = ptrnew(max);
    for (int i = 0; i < len; ++i)
	ptrdata(p)[i] = 'a' + i % 26;
    ptrdata(p)[len] = '\0';
    return ptrsetlen(p, len);
}

static void test_ptrdel(void)
{
    ptr *p = NULL;
    ptrdel(&p);
    check("ptrdel(NULL)", p == NULL);

    p = ptrnew(10);
    ptrdel(&p);
    check("ptrdel() clears the pointer", p == NULL);
}

static void test_ptrrchr(void)
{
    ptr *p = ptrmcpy(NULL, "abca", 4);
    check("ptrrchr() finds the last match", ptrrchr(p, 'a') == ptrdata(p) + 3);
    check("ptrrchr() finds a match in the middle",
	  ptrrchr(p, 'b') == ptrdata(p) + 1);
    ptrdel(&p);

    p = ptrmcpy(NULL, "abc", 3);
    check("ptrrchr() finds a match at index 0", ptrrchr(p, 'a') == ptrdata(p));
    check("ptrrchr() without a match", ptrrchr(p, 'x') == NULL);
    ptrdel(&p);
    check("ptrrchr(NULL)", ptrrchr(NULL, 'a') == NULL);
}

/* src is inside dst, and beyond the size that dst is about to shrink to */
static void test_overlap_shrink(void)
{
    ptr *p = new_alphabet(10000, 9000);
    char expect[11] = { 0 };
    memcpy(expect, ptrdata(p) + 8990, 10);
    p = ptrmcpy(p, ptrdata(p) + 8990, 10);
    check("ptrmcpy() from itself while shrinking", has_contents(p, expect));
    ptrdel(&p);

    p = new_alphabet(10000, 9000);
    ptrtrunc(p, 5);
    memcpy(expect, "abcde", 5);
    memcpy(expect + 5, ptrdata(p) + 8990, 5);
    expect[10] = '\0';
    p = ptrmcat(p, ptrdata(p) + 8990, 5);
    check("ptrmcat() from itself while shrinking", has_contents(p, expect));
    ptrdel(&p);
}

/* src is inside dst, which is about to be enlarged */
static void test_overlap_grow(void)
{
    ptr *p = ptrmcpy(NULL, "abc", 3);
    while (ptrlen(p) < 3000)
	p = ptrmcat(p, ptrdata(p), ptrlen(p));
    check("ptrmcat() from itself while growing",
	  ptrlen(p) == 3 * 1024 && memcmp(ptrdata(p) + 3069, "abc", 3) == 0);
    ptrdel(&p);
}

static void test_overflow(void)
{
    ptr *p = ptrmcpy(NULL, "abc", 3);

    error = 0;
    check("ptrnew(INT_MAX)", ptrnew(INT_MAX) == NULL && error == NO_MEM_ERROR);

    error = 0;
    check("ptrdup2(p, INT_MAX)",
	  ptrdup2(p, INT_MAX) == NULL && error == NO_MEM_ERROR);

    error = 0;
    p = ptrmcpy(p, "x", INT_MAX);
    check("ptrmcpy(INT_MAX)", error == NO_MEM_ERROR && has_contents(p, "abc"));

    error = 0;
    p = ptrmcat(p, "x", INT_MAX - 1);
    check("ptrmcat(INT_MAX - 1)",
	  error == NO_MEM_ERROR && has_contents(p, "abc"));

    error = 0;
    p = ptrpad(p, INT_MAX);
    check("ptrpad(INT_MAX)", error == NO_MEM_ERROR && has_contents(p, "abc"));

    error = 0;
    ptrdel(&p);
}

static void test_negative_lengths(void)
{
    ptr *p = ptrmcpy(NULL, "abc", 3);
    p = ptrpad(p, -1);
    check("ptrpad(-1) shortens", has_contents(p, "ab"));
    p = ptrpad(p, -10);
    check("ptrpad() stops at empty", has_contents(p, ""));
    p = ptrmcpy(p, "abc", 3);
    p = ptrsetlen(p, INT_MIN);
    check("ptrsetlen(INT_MIN) empties", has_contents(p, ""));
    ptrdel(&p);
}

int main(void)
{
    test_ptrdel();
    test_ptrrchr();
    test_overlap_shrink();
    test_overlap_grow();
    test_overflow();
    test_negative_lengths();
    if (failures != 0)
	printf("%d failures\n", failures);
    return failures != 0;
}
