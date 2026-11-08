#! /bin/sh

autoreconf -v --install || exit 1

# find the headers and libraries of these packages where pkg-config says
# they are, such as /usr/local on FreeBSD or /opt/homebrew on macOS
packages="gnutls libpcre2-posix libgrapheme"

pkg_config=${PKG_CONFIG:-pkg-config}
command -v "$pkg_config" >/dev/null 2>&1 || pkg_config=pkgconf

cppflags=$CPPFLAGS
ldflags=$LDFLAGS
if command -v "$pkg_config" >/dev/null 2>&1; then
    for p in $packages; do
        "$pkg_config" --exists "$p" || continue
        cppflags="$cppflags $("$pkg_config" --cflags-only-I "$p")"
        ldflags="$ldflags $("$pkg_config" --libs-only-L "$p")"
    done
fi

# passed as arguments so that "config.status --recheck" keeps them
./configure CPPFLAGS="${cppflags# }" LDFLAGS="${ldflags# }" "$@"
