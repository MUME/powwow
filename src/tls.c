#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "defines.h"
#include "main.h"
#include "tls.h"
#include "tty.h"
#include "utils.h"

#ifdef USE_TLS

#include <assert.h>
#include <errno.h>

#include <gnutls/gnutls.h>

#include "tcp.h"

struct tls {
    const struct io_fns *old_fns, **fnp;
    gnutls_session_t session;
    gnutls_certificate_credentials_t xcred;
    gnutls_anon_client_credentials_t anon_cred;
    bool session_ok;
    struct tls_options tlsopt;
};

static void tls_nofail(int error, const char *file, int line, const char *func)
{
    if (error >= 0)
        return;
    PRINTF("#%s:%d: %s: %s", file, line, func, gnutls_strerror(error));
    abort();
}

#define tls_nofail(expr) tls_nofail((expr), __FILE__, __LINE__, __func__)

static int tls_close(connsess *conn)
{
    if (conn->tls->session_ok) {
        gnutls_bye(conn->tls->session, GNUTLS_SHUT_RDWR);
        conn->tls->session_ok = false;
    }
    gnutls_deinit(conn->tls->session);
    int (*close_fn)(connsess *conn) = conn->tls->old_fns->close;
    *conn->tls->fnp = conn->tls->old_fns;
    free(conn->tls);
    conn->tls = NULL;
    return close_fn(conn);
}

static ssize_t tls_write(connsess *conn, const void *data, size_t len)
{
  ssize_t l = gnutls_record_send(conn->tls->session, data, len);
  if (l >= 0)
      ;
  else if (!gnutls_error_is_fatal(l))
      errno = EAGAIN;
  else {
      conn->tls->session_ok = false;
      errno = EINVAL;
  }
  return l;
}

static ssize_t tls_read(connsess *conn, void *data, size_t len)
{
  ssize_t l = gnutls_record_recv(conn->tls->session, data, len);
  if (l >= 0)
      ;
  else if (!gnutls_error_is_fatal(l))
      errno = EAGAIN;
  else {
      conn->tls->session_ok = false;
      errno = EINVAL;
  }
  return l;
}

static bool tls_can_read(connsess *conn)
{
    return gnutls_record_check_pending(conn->tls->session);
}

static const struct io_fns tls_io_fns = {
    .read     = tls_read,
    .write    = tls_write,
    .close    = tls_close,
    .can_read = tls_can_read
};

static void tls_log(int level, const char *msg)
{
    PRINTF("#TLS log %d: %s", level, msg);
}

static int cert_verify_callback(gnutls_session_t session)
{
    if (current_connection->tls->tlsopt.no_verify_ca)
        return 0;

    const char *host = current_connection->tls->tlsopt.hostname;
    if (host == NULL)
        host = current_connection->host;

    gnutls_typed_vdata_st data[] = {
        { .type = GNUTLS_DT_DNS_HOSTNAME, .data = (void *)host }
    };
    unsigned cert_status = 0;
    int rc = gnutls_certificate_verify_peers(
        session, data, sizeof data / sizeof data[0], &cert_status);
    if (rc == GNUTLS_E_NO_CERTIFICATE_FOUND) {
        PRINTF("#peer did not send any certificate.\n");
        return -1;
    }
    if (rc < 0) {
        PRINTF("#failed to verify certificate: %s\n",
               gnutls_strerror(rc));
        return -1;
    }

    if (cert_status != 0) {
        assert(cert_status & GNUTLS_CERT_INVALID);
        gnutls_certificate_type_t type = gnutls_certificate_type_get(session);
        gnutls_datum_t out;
        tls_nofail(gnutls_certificate_verification_status_print(
                       cert_status, type, &out, 0));
        PRINTF("#failed to verify certificate: %s\n", out.data);
        gnutls_free(out.data);
        return -1;
    }

    return 0;
}

bool tls_upgrade(struct connsess *conn, const struct tls_options *tlsopt)
{
    static bool did_init;
    if (!did_init) {
        tls_nofail(gnutls_global_init());
        gnutls_global_set_log_function(tls_log);
        did_init = true;
    }

    struct tls *tls = malloc(sizeof *tls);
    *tls = (struct tls){
        .old_fns = conn->fns,
        .fnp     = &conn->fns,
        .tlsopt  = *tlsopt
    };
    conn->fns = &tls_io_fns;
    conn->tls = tls;

    tls_nofail(
        gnutls_certificate_allocate_credentials(&tls->xcred));
    tls_nofail(
        gnutls_certificate_set_x509_system_trust(tls->xcred));
    tls_nofail(gnutls_anon_allocate_client_credentials(
                   &tls->anon_cred));

    tls_nofail(gnutls_init(&tls->session, GNUTLS_CLIENT));
    tls_nofail(gnutls_set_default_priority(tls->session));
    tls_nofail(gnutls_credentials_set(
                   tls->session, GNUTLS_CRD_ANON, tls->anon_cred));
    tls_nofail(gnutls_credentials_set(
                   tls->session, GNUTLS_CRD_CERTIFICATE, tls->xcred));

    gnutls_certificate_set_verify_function(
        tls->xcred, cert_verify_callback);

    gnutls_transport_set_int(tls->session, conn->fd);
    gnutls_handshake_set_timeout(tls->session, 0); /* disable timeout */

    for (;;) {
        connsess *old_conn = current_connection;
        current_connection = conn;
        int res = gnutls_handshake(tls->session);
        current_connection = old_conn;
        if (res >= 0) {
            tls->session_ok = true;
            return true;
        }
        if (!gnutls_error_is_fatal(res))
            continue;
        PRINTF("#TLS handshake failed: %s\n", gnutls_strerror(res));
        return false;
    }
}

#else  /* ! USE_TLS */

bool tls_upgrade(struct connsess *conn, const struct tls_options *tlsopt)
{
    return false;
}

#endif  /* ! USE_TLS */

static const char f_tls[] = "--tls";

#define TLSOPT_SEP ':'          /* update documentation if this changes */

/* returns false if invalid options;
   returns true and sets 'dst->use_tls' if present */
bool parse_tls_flag(struct tls_options *dst, const char **argp,
                    bool require_prefix)
{
    const char *arg = skipspace((char *)*argp);
    if (require_prefix) {
        if (strncmp(arg, f_tls, sizeof f_tls - 1) != 0)
            return true;
        arg += sizeof f_tls - 1;
        if (*arg == '=')
            ++arg;
        else if (*arg != 0)
            return true;
    }

#ifdef USE_TLS

    while (*arg != 0) {
        static const char f_no_verify[] = "no-verify";
        static const char f_host[] = "host=";

        if (strncmp(f_no_verify, arg, sizeof f_no_verify - 1) == 0) {
            arg += sizeof f_no_verify - 1;
            dst->no_verify_ca = true;
        } else if (strncmp(f_host, arg, sizeof f_host - 1) == 0) {
            arg += sizeof f_host - 1;
            const char *end = arg;
            while (*end != 0 && *end != TLSOPT_SEP && *end != ' ')
                ++end;
            size_t len = end - arg;
            free(dst->hostname);
            dst->hostname = malloc(len + 1);
            memcpy(dst->hostname, arg, len);
            dst->hostname[len] = 0;
            arg = end;
        } else
            return false;
        if (*arg == 0)
            break;
        if (*arg != TLSOPT_SEP)
            return false;
        ++arg;
    }

    dst->use_tls = true;
    return true;

#else  /* ! USE_TLS */

    PRINTF("%s is not supported in this version of powwow.\n", f_tls);
    return true;

#endif /* ! USE_TLS */
}

const char *tlsopt_str(const struct tls_options *tlsopt)
{
    if (!tlsopt->use_tls)
        return " " NO_TLS_FLAG;

    static char *buf;
    free(buf);
    buf = malloc(32 + (tlsopt->hostname ? strlen(tlsopt->hostname) : 0));
    char *p = buf;
    p += sprintf(p, " %s", f_tls);
    char prefix = '=';
    if (tlsopt->no_verify_ca) {
        p += sprintf(p, "%cno-verify", prefix);
        prefix = TLSOPT_SEP;
    }
    if (tlsopt->hostname != NULL) {
        p += sprintf(p, "%chost=%s", prefix, tlsopt->hostname);
        prefix = TLSOPT_SEP;
    }
    return buf;
}
