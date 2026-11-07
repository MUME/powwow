/*
 *  test_attr.c  --  unit tests for attr.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdio.h>
#include <string.h>

#include "attr.h"

static int failures;

/* print 's' with ESC shown as \e */
static void print_escaped(const char *s)
{
    for (const char *p = s; *p != '\0'; ++p)
        printf(*p == '\033' ? "\\e" : "%c", *p);
}

/*
 * parse 'text' and check that it selects 'sgr' and is described as
 * 'name', and that the description parses back to the same attribute
 */
static void check(const char *text, const char *sgr, const char *name)
{
    struct attr attr;
    if (!attr_parse(text, &attr)) {
        printf("FAIL \"%s\": rejected\n", text);
        ++failures;
        return;
    }

    char buf[ATTR_NAME_MAX];
    attr_sgr(&attr, buf, sizeof(buf));
    if (strcmp(buf, sgr) != 0) {
        printf("FAIL \"%s\": sequence \"", text);
        print_escaped(buf);
        printf("\"\n");
        ++failures;
    }

    attr_format(&attr, buf, sizeof(buf));
    if (strcmp(buf, name) != 0) {
        printf("FAIL \"%s\": name \"%s\"\n", text, buf);
        ++failures;
    }

    struct attr again;
    if (!attr_parse(buf, &again)
        || memcmp(&again, &attr, sizeof(attr)) != 0) {
        printf("FAIL \"%s\": \"%s\" does not parse back\n", text, buf);
        ++failures;
    }
}

static void check_invalid(const char *text)
{
    struct attr attr;
    if (attr_parse(text, &attr)) {
        printf("FAIL \"%s\": accepted\n", text);
        ++failures;
    }
}

#define E "\033"

int main(void)
{
    check("", "", "none");
    check("none", "", "none");
    check("none on none", "", "none");
    check("bold", E "[1m", "bold");
    check("BOLD  Underline", E "[1;4m", "bold underline");
    check("reverse", E "[7m", "inverse");
    check("dim italic hidden strike overline", E "[2;3;8;9;53m",
          "dim italic hidden strike overline");
    check("faint conceal strikethrough", E "[2;8;9m", "dim hidden strike");
    check("blink inverse", E "[5;7m", "blink inverse");

    check("red", E "[31m", "red");
    check("on blue", E "[44m", "on blue");
    check("yellow on cyan", E "[33;46m", "yellow on cyan");
    check("WHITE on BLACK", E "[97;100m", "WHITE on BLACK");
    check("bold red ON none", E "[1;31m", "bold red");
    check("none on green", E "[42m", "on green");

    check("208", E "[38;5;208m", "208");
    check("0 on 255", E "[38;5;0;48;5;255m", "0 on 255");
    check("bold 208 on 236", E "[1;38;5;208;48;5;236m", "bold 208 on 236");
    check("#ff8700", E "[38;2;255;135;0m", "#ff8700");
    check("italic on #003300", E "[3;48;2;0;51;0m", "italic on #003300");
    check("#FFFFFF on #000000", E "[38;2;255;255;255;48;2;0;0;0m",
          "#ffffff on #000000");
    check("underline RED on #101010", E "[4;91;48;2;16;16;16m",
          "underline RED on #101010");

    /* the longest attribute fits */
    check("bold blink underline inverse dim italic hidden strike overline"
          " #ffffff on #ffffff",
          E "[1;5;4;7;2;3;8;9;53;38;2;255;255;255;48;2;255;255;255m",
          "bold blink underline inverse dim italic hidden strike overline"
          " #ffffff on #ffffff");

    check_invalid("purple");
    check_invalid("Red");
    check_invalid("256");
    check_invalid("1000");
    check_invalid("-1");
    check_invalid("#fff");
    check_invalid("#ff870");
    check_invalid("#ff87000");
    check_invalid("#gg8700");
    check_invalid("red blue");
    check_invalid("red on");
    check_invalid("on");
    check_invalid("red on blue bold");
    check_invalid("red bold");

    if (failures == 0)
        printf("all attr tests passed\n");
    return failures != 0;
}
