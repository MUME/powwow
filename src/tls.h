#ifndef _TLS_H_
#define _TLS_H_

#include <stdbool.h>

struct connsess;
struct tls_options;
bool tls_upgrade(struct connsess *conn, const struct tls_options *tlsopt);

bool parse_tls_flag(struct tls_options *dst, const char **argp,
                    bool require_prefix);
const char *tlsopt_str(const struct tls_options *tlsopt);

#endif  /* _TLS_H_ */
