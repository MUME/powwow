#! /bin/sh
#
# build a self-contained tree of powwow for Windows, with the Cygwin DLLs,
# mintty and sh, for an installer (see powwow.iss) or a zip
#
# run in Cygwin, in a build directory configured with --prefix=/usr, after
# make:
#
#     cygwin/bundle.sh [DIR]       (default: powwow-win)
#
# Cygwin takes the parent of the directory of cygwin1.dll as its root, so
# DIR mirrors a Cygwin root: the programs and DLLs in DIR/bin, the data in
# DIR/usr/share/powwow

set -e

case $(uname -s) in
    CYGWIN*) ;;
    *) echo "$0: run this in Cygwin" >&2; exit 1 ;;
esac

if [ ! -f Makefile ]; then
    echo "$0: run this in the build directory, after configure and" \
         "make" >&2
    exit 1
fi
prefix=$(sed -n 's/^prefix = //p' Makefile)
if [ "$prefix" != /usr ]; then
    echo "$0: configure with --prefix=/usr, not $prefix" >&2
    exit 1
fi

stage=$(realpath -m "${1:-powwow-win}")
rm -rf "$stage"
make install-strip DESTDIR="$stage"

# the programs go in /bin, which Cygwin also mounts as /usr/bin; nothing on
# Windows reads the headers or the man page
mkdir -p "$stage/bin"
mv "$stage"/usr/bin/* "$stage/bin/"
rmdir "$stage/usr/bin"
rm -rf "$stage/usr/include" "$stage/usr/share/man"

# mintty to run powwow in, sh for #!, #var $x=!... and the editor, and
# cygwin-console-helper, which Cygwin runs to give a pseudo terminal the
# hidden console that Windows needs: without it, Windows shows powwow in a
# console window of its own besides mintty
for p in mintty sh cygwin-console-helper; do
    cp -L "/usr/bin/$p.exe" "$stage/bin/"
done

# the DLLs of all the programs, of their DLLs too, and cygwin1.dll
for exe in "$stage"/bin/*.exe; do
    ldd "$exe"
done | sed -n 's|.* => \(/[^ ]*\.dll\) .*|\1|p' | grep -v '^/cygdrive/' \
    | sort -u | while read -r dll; do
    cp -L "$dll" "$stage/bin/"
done

# the terminals that mintty and the Windows console set in TERM
for t in xterm xterm-256color cygwin; do
    for f in /usr/share/terminfo/*/"$t"; do
        [ -e "$f" ] || continue
        mkdir -p "$stage${f%/*}"
        cp -L "$f" "$stage${f%/*}/"
    done
done

# the CA certificates, at the path that GnuTLS was built to read them from
gnutls=$(ls "$stage"/bin/cyggnutls-*.dll 2>/dev/null | head -n 1)
trust=
if [ -n "$gnutls" ]; then
    trust=$(grep -aoE '/etc/[[:graph:]]+\.(crt|pem)' "$gnutls" | head -n 1)
fi
trust=${trust:-/etc/pki/tls/certs/ca-bundle.crt}
if [ -e "$trust" ]; then
    mkdir -p "$stage${trust%/*}"
    cp -L "$trust" "$stage$trust"
else
    echo "$0: warning: no CA certificates at $trust;" \
         "install ca-certificates, or TLS cannot check servers" >&2
fi

mkdir -p "$stage/etc"
cat > "$stage/etc/nsswitch.conf" <<'EOF'
# HOME is the Windows profile directory, such as C:\Users\name, where
# powwow keeps its definition files
db_home: windows
EOF
cat > "$stage/etc/fstab" <<'EOF'
# /tmp is the temporary directory of the Windows user, as the installation
# directory may not be writable
none /tmp usertemp binary,posix=0 0 0
none /cygdrive cygdrive binary,posix=0,user 0 0
EOF
# mintty draws the pictographic symbols, such as emoji, which most fonts
# lack, with the emoji font of Windows
cat > "$stage/etc/minttyrc" <<'EOF'
Font2=Segoe UI Emoji
FontChoice=PictoSymbols:2
EOF

# the Cygwin packages of the files bundled, for their licenses and sources
doc=$stage/usr/share/doc/powwow
mkdir -p "$doc"
{
    echo "Cygwin packages of the programs, DLLs and data bundled with"
    echo "powwow; their sources are at https://cygwin.com/packages/"
    echo
    for f in "$stage"/bin/* "$stage"/usr/share/terminfo/*/* \
             "$stage$trust"; do
        [ -e "$f" ] || continue
        case $f in
            "$stage"/bin/*) f=/usr/bin/${f##*/} ;;
            *) f=${f#"$stage"} ;;
        esac
        cygcheck -f "$f"
    done | sort -u
} > "$doc/PACKAGES"

echo
echo "created $stage"
echo "build the installer on Windows with Inno Setup:"
echo "    ISCC /DStage=$(cygpath -w "$stage")" \
     "$(cygpath -w "$(dirname "$0")")\\powwow.iss"
