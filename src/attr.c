/*
 *  attr.c  --  parse, describe and select text attributes (colors and
 *              effects such as bold)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "attr.h"

struct effect {
    const char *name;
    enum attr_flag flag;
    int sgr;                    /* SGR parameter selecting it */
};

/* in the order attr_format() lists them */
static const struct effect effects[] = {
    { .name = "bold",          .flag = ATTR_BOLD,      .sgr = 1 },
    { .name = "blink",         .flag = ATTR_BLINK,     .sgr = 5 },
    { .name = "underline",     .flag = ATTR_UNDERLINE, .sgr = 4 },
    { .name = "inverse",       .flag = ATTR_INVERSE,   .sgr = 7 },
    { .name = "dim",           .flag = ATTR_DIM,       .sgr = 2 },
    { .name = "italic",        .flag = ATTR_ITALIC,    .sgr = 3 },
    { .name = "hidden",        .flag = ATTR_HIDDEN,    .sgr = 8 },
    { .name = "strike",        .flag = ATTR_STRIKE,    .sgr = 9 },
    { .name = "overline",      .flag = ATTR_OVERLINE,  .sgr = 53 },
};

#define N_EFFECTS (sizeof(effects) / sizeof(effects[0]))

/* other names accepted for effects */
static const struct effect effect_aliases[] = {
    { .name = "reverse",       .flag = ATTR_INVERSE },
    { .name = "faint",         .flag = ATTR_DIM },
    { .name = "conceal",       .flag = ATTR_HIDDEN },
    { .name = "strikethrough", .flag = ATTR_STRIKE },
};

#define N_EFFECT_ALIASES (sizeof(effect_aliases) / sizeof(effect_aliases[0]))

/* lowercase are the ANSI colors, uppercase their bright variants */
static const char *const color_names[16] = {
    "black", "red", "green", "yellow", "blue", "magenta", "cyan", "white",
    "BLACK", "RED", "GREEN", "YELLOW", "BLUE", "MAGENTA", "CYAN", "WHITE"
};

static const char WHITESPACE[] = " \t";

/* is the token [tok, tok + len) equal to 'word'? */
static bool token_is(const char *tok, size_t len, const char *word,
                     bool ignore_case)
{
    if (strlen(word) != len)
        return false;
    return (ignore_case
            ? strncasecmp(tok, word, len) == 0
            : strncmp(tok, word, len) == 0);
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* parse the color [tok, tok + len) into *color; false if it is none */
static bool parse_color(const char *tok, size_t len, struct color *color)
{
    if (token_is(tok, len, "none", false)) {
        *color = (struct color){ .kind = COLOR_NONE };
        return true;
    }
    for (unsigned i = 0; i < 16; ++i) {
        if (token_is(tok, len, color_names[i], false)) {
            *color = (struct color){ .kind = COLOR_NAMED, .value = i };
            return true;
        }
    }
    if (len >= 1 && len <= 3 && strspn(tok, "0123456789") >= len) {
        uint32_t value = 0;
        for (size_t i = 0; i < len; ++i)
            value = value * 10 + (uint32_t)(tok[i] - '0');
        if (value > 255)
            return false;
        *color = (struct color){ .kind = COLOR_INDEX, .value = value };
        return true;
    }
    if (len == 7 && tok[0] == '#') {
        uint32_t value = 0;
        for (size_t i = 1; i < len; ++i) {
            int d = hex_digit(tok[i]);
            if (d < 0)
                return false;
            value = (value << 4) | (uint32_t)d;
        }
        *color = (struct color){ .kind = COLOR_RGB, .value = value };
        return true;
    }
    return false;
}

/* return the flag of the effect [tok, tok + len), or 0 */
static enum attr_flag effect_flag(const char *tok, size_t len)
{
    for (size_t i = 0; i < N_EFFECTS; ++i) {
        if (token_is(tok, len, effects[i].name, true))
            return effects[i].flag;
    }
    for (size_t i = 0; i < N_EFFECT_ALIASES; ++i) {
        if (token_is(tok, len, effect_aliases[i].name, true))
            return effect_aliases[i].flag;
    }
    return 0;
}

/* advance *text to the next token and return its length, 0 at the end */
static size_t next_token(const char **text)
{
    *text += strspn(*text, WHITESPACE);
    return strcspn(*text, WHITESPACE);
}

bool attr_parse(const char *text, struct attr *attr)
{
    struct attr a = { 0 };
    size_t len = next_token(&text);

    /* effects */
    while (len > 0) {
        enum attr_flag flag = effect_flag(text, len);
        if (flag == 0)
            break;
        a.flags |= flag;
        text += len;
        len = next_token(&text);
    }

    /* [foreground] */
    if (len > 0 && !token_is(text, len, "on", true)) {
        if (!parse_color(text, len, &a.fg))
            return false;
        text += len;
        len = next_token(&text);
    }

    /* [on background] */
    if (len > 0) {
        if (!token_is(text, len, "on", true))
            return false;
        text += len;
        len = next_token(&text);
        if (len == 0 || !parse_color(text, len, &a.bg))
            return false;
        text += len;
        len = next_token(&text);
    }

    if (len > 0)
        return false;
    *attr = a;
    return true;
}

bool attr_is_none(const struct attr *attr)
{
    return (attr->flags == 0 && attr->fg.kind == COLOR_NONE
            && attr->bg.kind == COLOR_NONE);
}

/* append printf-style output to 'buf', which holds *len characters */
static void append(char *buf, size_t size, size_t *len,
                   const char *format, ...)
{
    if (*len >= size)
        return;
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buf + *len, size - *len, format, ap);
    va_end(ap);
    if (n > 0)
        *len += (size_t)n;
    if (*len >= size)
        *len = size - 1;
}

/* append the SGR parameters selecting 'color'; base is 30 or 40 */
static void append_color_params(char *buf, size_t size, size_t *len,
                                const struct color *color, int base)
{
    const char *sep = *len > 0 ? ";" : "";
    uint32_t v = color->value;

    switch (color->kind) {
    case COLOR_NONE:
        break;
    case COLOR_NAMED:
        append(buf, size, len, "%s%d", sep,
               (v < 8 ? base + (int)v : base + 60 + (int)v - 8));
        break;
    case COLOR_INDEX:
        append(buf, size, len, "%s%d;5;%u", sep, base + 8, (unsigned)v);
        break;
    case COLOR_RGB:
        append(buf, size, len, "%s%d;2;%u;%u;%u", sep, base + 8,
               (unsigned)(v >> 16) & 0xff, (unsigned)(v >> 8) & 0xff,
               (unsigned)v & 0xff);
        break;
    }
}

void attr_sgr_params(const struct attr *attr, char *buf, size_t size)
{
    size_t len = 0;

    if (size == 0)
        return;
    buf[0] = '\0';
    for (size_t i = 0; i < N_EFFECTS; ++i) {
        if (attr->flags & effects[i].flag)
            append(buf, size, &len, "%s%d", len > 0 ? ";" : "",
                   effects[i].sgr);
    }
    append_color_params(buf, size, &len, &attr->fg, 30);
    append_color_params(buf, size, &len, &attr->bg, 40);
}

void attr_sgr(const struct attr *attr, char *buf, size_t size)
{
    char params[ATTR_SGR_MAX];

    if (size == 0)
        return;
    buf[0] = '\0';
    if (attr_is_none(attr))
        return;
    attr_sgr_params(attr, params, sizeof(params));
    snprintf(buf, size, "\033[%sm", params);
}

/* append the description of 'color' */
static void append_color(char *buf, size_t size, size_t *len,
                         const struct color *color)
{
    switch (color->kind) {
    case COLOR_NONE:
        append(buf, size, len, "none");
        break;
    case COLOR_NAMED:
        append(buf, size, len, "%s", color_names[color->value & 15]);
        break;
    case COLOR_INDEX:
        append(buf, size, len, "%u", (unsigned)color->value);
        break;
    case COLOR_RGB:
        append(buf, size, len, "#%06x", (unsigned)color->value & 0xffffff);
        break;
    }
}

void attr_format(const struct attr *attr, char *buf, size_t size)
{
    size_t len = 0;

    if (size == 0)
        return;
    buf[0] = '\0';
    if (attr_is_none(attr)) {
        append(buf, size, &len, "none");
        return;
    }
    for (size_t i = 0; i < N_EFFECTS; ++i) {
        if (attr->flags & effects[i].flag)
            append(buf, size, &len, "%s%s", len > 0 ? " " : "",
                   effects[i].name);
    }
    if (attr->fg.kind != COLOR_NONE) {
        if (len > 0)
            append(buf, size, &len, " ");
        append_color(buf, size, &len, &attr->fg);
    }
    if (attr->bg.kind != COLOR_NONE) {
        append(buf, size, &len, "%son ", len > 0 ? " " : "");
        append_color(buf, size, &len, &attr->bg);
    }
}
