/*
 *  test_secret.c  --  unit tests for secret.c and sha256.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "secret.h"
#include "sha256.h"
#include "ptr.h"
#include "utils.h"

/* needed by secret.c */
void *xrealloc(void *p, size_t size)
{
    void *res = realloc(p, size);
    if (res == NULL)
        abort();
    return res;
}

static int failures;

static void check(const char *what, bool ok)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        ++failures;
    }
}

/* the SHA-256 of the 'len' bytes at 's', in hex */
static void sha256_hex(const char *s, size_t len, int repeat, char hex[65])
{
    SHA256_CTX ctx;
    unsigned char digest[SHA256_BLOCK_SIZE];
    sha256_init(&ctx);
    for (int i = 0; i < repeat; ++i)
        sha256_update(&ctx, (const unsigned char *)s, len);
    sha256_final(&ctx, digest);
    for (int i = 0; i < SHA256_BLOCK_SIZE; ++i)
        sprintf(hex + (size_t)2 * i, "%02x", digest[i]);
}

/* the examples of FIPS 180-4, and a million times "a" */
static void test_sha256(void)
{
    static const struct {
        const char *text;
        int repeat;
        const char *hash;
    } tests[] = {
        { "", 1,
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
        { "abc", 1,
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
        { "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 1,
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1" },
        { "a", 1000000,
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0" },
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        char hex[65];
        sha256_hex(tests[i].text, strlen(tests[i].text), tests[i].repeat,
                   hex);
        check(tests[i].hash, strcmp(hex, tests[i].hash) == 0);
    }
}

static const struct secret_salt salt = {
    { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef }
};

static void test_format(void)
{
    struct secret read;
    struct secret sec = secret_make(&salt, "hunter2", 7);
    char buf[SECRET_STR_MAX];
    secret_format(&sec, buf);
    check("secret_format() starts with the salt",
          strncmp(buf, "0123456789abcdef:7:", 19) == 0);
    check("secret_format() ends with 8 hex digits",
          strlen(buf) == 19 + 8 && strspn(buf + 19, "0123456789abcdef") == 8);
    check("secret_parse() reads what secret_format() writes",
          secret_parse(&read, buf) && secret_is(&read, "hunter2", 7));

    check("secret_parse() needs the hash",
          !secret_parse(&read, "0123456789abcdef:7:"));
    check("secret_parse() needs the length",
          !secret_parse(&read, "0123456789abcdef::01234567"));
    check("secret_parse() needs 8 bytes of salt",
          !secret_parse(&read, "0123456789abcd:7:01234567"));
    check("secret_parse() rejects a short secret",
          !secret_parse(&read, "0123456789abcdef:3:01234567"));
    check("secret_parse() rejects text after the hash",
          !secret_parse(&read, "0123456789abcdef:7:01234567x"));
    check("secret_parse() rejects a non-hex digit",
          !secret_parse(&read, "0123456789abcdeg:7:01234567"));
    check("secret_parse() accepts upper case hex",
          secret_parse(&read, "0123456789ABCDEF:7:0123ABCD")
          && read.hash == 0x0123abcd && read.salt.bytes[7] == 0xef);
}

static void test_match(void)
{
    struct secret sec = secret_make(&salt, "hunter2", 7);
    check("secret_is() the secret", secret_is(&sec, "hunter2", 7));
    check("secret_is() not a prefix", !secret_is(&sec, "hunter", 6));
    check("secret_is() not another word", !secret_is(&sec, "hunter3", 7));

    struct secret_salt zero = { { 0 } };
    struct secret other = secret_make(&zero, "hunter2", 7);
    check("the salt changes the hash", other.hash != sec.hash);

    check("no secrets, no match", !secret_in("hunter2"));
    check("secret_add() adds", secret_add(&sec));
    check("secret_add() not twice", !secret_add(&sec));
    check("secret_add() the same secret with another salt",
          secret_add(&other));
    check("two secrets", secret_count() == 2);
    secret_delete(1);
    check("secret_delete()", secret_count() == 1);

    struct secret longer = secret_make(&salt, "swordfish", 9);
    secret_add(&longer);

    check("secret_in() the whole line", secret_in("hunter2"));
    check("secret_in() at the start", secret_in("hunter2 is it"));
    check("secret_in() in the middle", secret_in("say password=hunter2!"));
    check("secret_in() at the end", secret_in("say hunter2"));
    check("secret_in() the longer one", secret_in("-swordfish-"));
    check("secret_in() not a part", !secret_in("hunter swordfis"));
    check("secret_in() not in a short line", !secret_in("hun"));
    check("secret_in() not an empty line", !secret_in(""));

    check("secret_find()", secret_find("swordfish", 9) == 1);
    check("secret_find() nothing", secret_find("swordfist", 9) == -1);
}

static void test_new_salt(void)
{
    struct secret_salt a = secret_new_salt();
    struct secret_salt b = secret_new_salt();
    check("secret_new_salt() differs", memcmp(&a, &b, sizeof(a)) != 0);
}

int main(void)
{
    test_sha256();
    test_format();
    test_match();
    test_new_salt();
    return failures != 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
