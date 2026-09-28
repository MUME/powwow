/*
 *  test_sgr.c  --  unit tests for sgr.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <string.h>

#include "sgr.h"

static int failures;

static void check(const char *what, const char *text, const char *expect)
{
    struct sgr_state st;
    sgr_reset(&st);
    sgr_apply(&st, text, strlen(text));
    if (strcmp(st.seq, expect) != 0 || st.len != strlen(expect)) {
        printf("FAIL %s: got \"", what);
        for (const char *p = st.seq; *p; ++p)
            printf(*p == '\033' ? "\\e" : "%c", *p);
        printf("\"\n");
        ++failures;
    }
}

#define E "\033"

int main(void)
{
    check("plain text", "hello", "");
    check("bold", E "[1mx", E "[1m");
    check("bold then reset", E "[1mx" E "[0m", "");
    check("empty reset", E "[1mx" E "[m", "");
    check("accumulate", E "[1m" E "[32mx", E "[1m" E "[32m");
    check("reset inside", E "[1m" E "[0;32mx", E "[32m");
    check("reset at end", E "[1;32;0m", "");
    check("bug0 prompt", E "[1m002_" E "[0m \xe2\x94\x82 " E "[32m!" E "[0m \xe2\x94\x82 " E "[32m",
          E "[32m");
    check("256 color with 0 index", E "[38;5;0m", E "[38;5;0m");
    check("truecolor with 0", E "[38;2;0;0;0m", E "[38;2;0;0;0m");
    check("256 color then reset", E "[38;5;240m" E "[0m", "");
    check("colon form", E "[38:5:0m", E "[38:5:0m");
    check("other CSI ignored", E "[2K" E "[1m" E "[10;5H", E "[1m");
    check("incomplete", E "[1m" E "[3", E "[1m");
    check("full reset", E "[1m" E "c", "");

    /* overflow drops the oldest sequences */
    char big[4096] = "";
    for (int i = 0; i < 100; ++i)
        strcat(big, E "[31m");
    strcat(big, E "[1m");
    struct sgr_state st;
    sgr_reset(&st);
    sgr_apply(&st, big, strlen(big));
    if (st.len >= SGR_STATE_MAX || strlen(st.seq) != st.len
        || strcmp(st.seq + st.len - 4, E "[1m") != 0) {
        printf("FAIL overflow: len %zu\n", st.len);
        ++failures;
    }

    if (failures == 0)
        printf("all sgr tests passed\n");
    return failures != 0;
}
