/*
 *  secret.c  --  hashes of the secrets kept out of the history
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "defines.h"
#include "secret.h"
#include "sha256.h"
#include "ptr.h"
#include "utils.h"

static struct secret *secrets;
static int nsecrets, secrets_size;

struct secret_salt secret_new_salt(void)
{
    struct secret_salt salt;
    size_t got = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        while (got < SECRET_SALT_LEN) {
            ssize_t n = read(fd, salt.bytes + got, SECRET_SALT_LEN - got);
            if (n <= 0)
                break;
            got += n;
        }
        close(fd);
    }
    /* the salt need not be secret, only different for each secret */
    for (; got < SECRET_SALT_LEN; ++got)
        salt.bytes[got] = (uint8_t)(lrand48() >> 8);
    return salt;
}

/*
 * the first 32 bits of SHA-256(salt + secret): so few that a strong
 * secret cannot be told from the many other strings with the same hash
 * if a definition file leaks, but enough that other text matches it only
 * rarely; the salt makes the hashes of a secret differ from file to file,
 * and from any table computed in advance
 */
static uint32_t secret_hash(const struct secret_salt *salt, const char *s,
                            size_t len)
{
    SHA256_CTX ctx;
    uint8_t digest[SHA256_BLOCK_SIZE];
    sha256_init(&ctx);
    sha256_update(&ctx, salt->bytes, SECRET_SALT_LEN);
    sha256_update(&ctx, (const uint8_t *)s, len);
    sha256_final(&ctx, digest);
    return ((uint32_t)digest[0] << 24 | (uint32_t)digest[1] << 16
            | (uint32_t)digest[2] << 8 | digest[3]);
}

struct secret secret_make(const struct secret_salt *salt,
                          const char *s, size_t len)
{
    return (struct secret){
        .salt = *salt,
        .len = len,
        .hash = secret_hash(salt, s, len),
    };
}

bool secret_is(const struct secret *sec, const char *s, size_t len)
{
    return len == sec->len && secret_hash(&sec->salt, s, len) == sec->hash;
}

void secret_format(const struct secret *sec, char buf[static SECRET_STR_MAX])
{
    char *p = buf;
    for (int i = 0; i < SECRET_SALT_LEN; ++i)
        p += sprintf(p, "%02x", sec->salt.bytes[i]);
    snprintf(p, SECRET_STR_MAX - (p - buf), ":%zu:%08" PRIx32,
             sec->len, sec->hash);
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

/* read 'n' hex digits at 's' into 'value'; false if any is not one */
static bool parse_hex(const char *s, int n, uint32_t *value)
{
    *value = 0;
    for (int i = 0; i < n; ++i) {
        int d = hex_digit(s[i]);
        if (d < 0)
            return false;
        *value = *value << 4 | (uint32_t)d;
    }
    return true;
}

bool secret_parse(struct secret *sec, const char *str)
{
    const char *s = str;
    for (int i = 0; i < SECRET_SALT_LEN; ++i, s += 2) {
        uint32_t value;
        if (!parse_hex(s, 2, &value))
            return false;
        sec->salt.bytes[i] = (uint8_t)value;
    }
    if (*s++ != ':')
        return false;

    size_t len = 0;
    const char *digits = s;
    while (*s >= '0' && *s <= '9' && s - digits < 6)
        len = len * 10 + (size_t)(*s++ - '0');
    if (s == digits || len < SECRET_MIN_LEN || *s++ != ':')
        return false;
    sec->len = len;

    return parse_hex(s, 8, &sec->hash) && s[8] == '\0';
}

int secret_count(void)
{
    return nsecrets;
}

const struct secret *secret_get(int i)
{
    assert(0 <= i && i < nsecrets);
    return &secrets[i];
}

bool secret_add(const struct secret *sec)
{
    for (int i = 0; i < nsecrets; ++i) {
        const struct secret *old = &secrets[i];
        if (memcmp(&old->salt, &sec->salt, sizeof(old->salt)) == 0
            && old->len == sec->len && old->hash == sec->hash)
            return false;
    }
    if (nsecrets == secrets_size) {
        secrets_size = secrets_size != 0 ? secrets_size * 2 : 8;
        secrets = xrealloc(secrets, sizeof(secrets[0]) * secrets_size);
    }
    secrets[nsecrets++] = *sec;
    return true;
}

void secret_delete(int i)
{
    assert(0 <= i && i < nsecrets);
    --nsecrets;
    memmove(secrets + i, secrets + i + 1,
            sizeof(secrets[0]) * (nsecrets - i));
}

int secret_find(const char *s, size_t len)
{
    for (int i = 0; i < nsecrets; ++i) {
        if (secret_is(&secrets[i], s, len))
            return i;
    }
    return -1;
}

bool secret_in(const char *line)
{
    size_t line_len = strlen(line);
    if (nsecrets == 0 || line_len < SECRET_MIN_LEN)
        return false;
    for (int i = 0; i < nsecrets; ++i) {
        const struct secret *sec = &secrets[i];
        for (size_t start = 0; start + sec->len <= line_len; ++start) {
            if (secret_is(sec, line + start, sec->len))
                return true;
        }
    }
    return false;
}
