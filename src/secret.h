/* public things from secret.c */

#ifndef _SECRET_H_
#define _SECRET_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * A secret, such as a password, that no line kept in the history or
 * elsewhere may contain. Only a salted hash of it is kept: the first 32
 * bits of SHA-256(salt + secret), and its length.
 */
#define SECRET_MIN_LEN  4       /* shorter ones would match too often */
#define SECRET_WARN_LEN 8       /* shorter ones may match other text, and are
                                 * easy to find from their hash */
#define SECRET_SALT_LEN 8
#define SECRET_STR_MAX  64      /* "<salt>:<length>:<hash>" and a '\0' */

struct secret_salt {
    uint8_t bytes[SECRET_SALT_LEN];
};

struct secret {
    struct secret_salt salt;
    size_t len;
    uint32_t hash;
};

/* a new salt of random bytes */
struct secret_salt secret_new_salt(void);
/* the secret of the 'len' bytes at 's', with 'salt' */
struct secret secret_make(const struct secret_salt *salt,
                          const char *s, size_t len);
/* whether the 'len' bytes at 's' are the secret 'sec' */
bool secret_is(const struct secret *sec, const char *s, size_t len);
/* write 'sec' into 'buf' as "<salt>:<length>:<hash>", in hex but the length */
void secret_format(const struct secret *sec, char buf[static SECRET_STR_MAX]);
/* read 'str' as written by secret_format() into 'sec'; false if invalid */
bool secret_parse(struct secret *sec, const char *str);

/* the secrets kept */
int secret_count(void);
const struct secret *secret_get(int i);
/* add 'sec', unless it is kept already; return whether it was added */
bool secret_add(const struct secret *sec);
void secret_delete(int i);
/* the index of the secret that the 'len' bytes at 's' are, or -1 */
int secret_find(const char *s, size_t len);
/* whether 'line' contains one of the secrets */
bool secret_in(const char *line);

#endif /* _SECRET_H_ */
