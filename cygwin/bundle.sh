#! /bin/sh
#
# build a self-contained tree of powwow for Windows, with the Cygwin DLLs,
# mintty and sh, for an installer (see powwow.iss) or a zip
#
# run in Cygwin, in a build directory configured with the prefix /usr or
# /usr/local, the default, after make:
#
#     cygwin/bundle.sh [DIR]       (default: powwow-win)
#
# Cygwin takes the parent of the directory of cygwin1.dll as its root, so
# DIR mirrors a Cygwin root: the programs and DLLs in DIR/bin, the data in
# DIR/PREFIX/share/powwow, where powwow looks for it. The license files
# of the Cygwin packages bundled come from cygwin-licenses in the build
# directory, which update-licenses.sh makes; for those it lacks, it is
# run, which downloads their source packages

set -e
# V=1, such as from make V=1, shows the commands as they run
[ "${V:-0}" = 0 ] || set -x

case $(uname -s) in
    CYGWIN*) ;;
    *) echo "$0: run this in Cygwin" >&2; exit 1 ;;
esac

if [ ! -f Makefile ]; then
    echo "$0: run this in the build directory, after configure and" \
         "make" >&2
    exit 1
fi
# the prefix that powwow looks for its data under, which must be in the
# bundle: other prefixes may be mounted elsewhere, such as /tmp or
# /cygdrive
prefix=$(sed -n 's/^prefix = //p' Makefile)
case $prefix in
    /usr|/usr/local) ;;
    *)
        echo "$0: configure with the prefix /usr or /usr/local, not" \
             "$prefix" >&2
        exit 1
        ;;
esac

stage=$(realpath -m "${1:-powwow-win}")
rm -rf "$stage"
make install-strip DESTDIR="$stage"

# the programs go in /bin, which Cygwin also mounts as /usr/bin; nothing on
# Windows reads the headers or the man page
mkdir -p "$stage/bin"
mv "$stage$prefix"/bin/* "$stage/bin/"
rmdir "$stage$prefix/bin"
rm -rf "$stage$prefix/include" "$stage$prefix/share/man"

# mintty to run powwow in, sh for #!, #var $x=!... and the editor,
# cygpath for powwow-start to find the Documents folder, and
# cygwin-console-helper, which Cygwin runs to give a pseudo terminal the
# hidden console that Windows needs: without it, Windows shows powwow in a
# console window of its own besides mintty
for p in mintty sh cygpath cygwin-console-helper; do
    cp -L "/usr/bin/$p.exe" "$stage/bin/"
done

# what the shortcuts and .powwow files run in mintty, and the shell and
# editor it sets
cp "$(dirname "$0")/powwow-start" "$(dirname "$0")/powwow-cmd" \
    "$(dirname "$0")/powwow-notepad" "$stage/bin/"

# less, the pager of #help and of the texts that the MUD shows, which
# powwow-start sets in POWWOWPAGER; without one, the more.com of Windows
# is found, which cannot page in mintty
if [ ! -e /usr/bin/less.exe ]; then
    echo "$0: no /usr/bin/less.exe; install the Cygwin package less" >&2
    exit 1
fi
cp -L /usr/bin/less.exe "$stage/bin/"
# nano, the editor that powwow-start sets in an ssh session, where Notepad
# would not be seen
if [ ! -e /usr/bin/nano.exe ]; then
    echo "$0: no /usr/bin/nano.exe; install the Cygwin package nano" >&2
    exit 1
fi
cp -L /usr/bin/nano.exe "$stage/bin/"
# and more, powwow's default pager, as less, for when powwow-start did not
# set POWWOWPAGER, such as when powwow.exe is run itself
cp "$(dirname "$0")/more" "$stage/bin/"

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

# the CA certificates, which powwow reads from this file, as GnuTLS loads
# those of the system through the trust module of p11-kit, not bundled
trust=/etc/pki/tls/certs/ca-bundle.crt
if [ -e "$trust" ]; then
    mkdir -p "$stage${trust%/*}"
    cp -L "$trust" "$stage$trust"
else
    echo "$0: warning: no CA certificates at $trust;" \
         "install ca-certificates, or TLS cannot check servers" >&2
fi

# the policy of the system that GnuTLS takes its default priorities from;
# without it, powwow falls back to the priorities that GnuTLS calls NORMAL
gnutls=$(ls "$stage"/bin/cyggnutls-*.dll 2>/dev/null | head -n 1)
policy=
if [ -n "$gnutls" ]; then
    policy=$(grep -aoE '/etc/[[:graph:]]+/gnutls\.config' "$gnutls" \
                 | head -n 1)
fi
if [ -n "$policy" ] && [ -e "$policy" ]; then
    mkdir -p "$stage${policy%/*}"
    cp -L "$policy" "$stage$policy"
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

# the packages of the files bundled, as "PACKAGE VERSION FILE"; the files
# that no package installed are powwow's own
# the lists below go in files, which awk reads, rather than in variables,
# whose text set -x, as by make V=1, would show at every use
lists=$(mktemp -d)
trap 'rm -rf "$lists"' EXIT
awk 'NR > 1 { v = $2; sub(/\.tar\.[a-z0-9]+$/, "", v);
              print v, $1, substr(v, length($1) + 2) }' \
    /etc/setup/installed.db > "$lists/installed"
find "$stage" -type f | sort | while read -r f; do
    file=${f#"$stage"}
    case $file in
        (/bin/*) path=/usr$file ;;
        (*) path=$file ;;
    esac
    full=$(cygcheck -f "$path" 2> /dev/null | head -n 1)
    if [ -n "$full" ]; then
        awk -v full="$full" -v file="$file" \
            '$1 == full { print $2, $3, file; exit }' "$lists/installed"
        continue
    fi
    # the files that the postinstall scripts of packages make
    case $file in
        (/etc/pki/*) pkg=ca-certificates ;;
        (/etc/crypto-policies/*) pkg=crypto-policies ;;
        (*) continue ;;
    esac
    awk -v pkg="$pkg" -v file="$file" \
        '$2 == pkg { print $2, $3, file; exit }' "$lists/installed"
done > "$lists/files"

# their license files, from cygwin-licenses, which update-licenses.sh
# makes, by the lines "PACKAGE SOURCE LICENSE" of its table; for the
# packages that it lacks, update-licenses.sh downloads their source
# packages to add them
here=$(dirname "$0")
got=$(pwd)/cygwin-licenses
table=$got/PACKAGES
mkdir -p "$got"
[ -f "$table" ] || : > "$table"
packages=$(cut -d ' ' -f 1 "$lists/files" | sort -u)
missing=$(for pkg in $packages; do
              awk -v p="$pkg" '$1 == p { found = 1; exit }
                               END { if (!found) print p }' "$table"
          done)
if [ -n "$missing" ]; then
    echo "getting the licenses of" $missing "from their Cygwin source" \
         "packages"
    if ! "$here/update-licenses.sh" $missing; then
        echo "$0: cannot get the licenses of" $missing >&2
        exit 1
    fi
fi
licenses=$stage/usr/share/doc/powwow/licenses
mkdir -p "$licenses"
for pkg in $packages; do
    awk -v p="$pkg" '$1 == p { print; exit }' "$table"
done > "$lists/sources"
names=$(cut -d ' ' -f 2 "$lists/sources" | sort -u)
for name in $names; do
    cp -R "$got/$name" "$licenses/"
done

# the list of them, which the installer and #help copyright show
{
    cat <<'END'
Licenses of powwow for Windows
==============================

powwow itself is free software under the GNU General Public License,
version 2 or later: see COPYING, which #help copyright shows. Its source
is at https://github.com/MUME/powwow.

unicode-names.txt and emoji-names.txt are made from the Unicode
Character Database and the emoji data of Unicode, under the Unicode
License v3: see unicode-license.txt.

The other programs and libraries here are those of Cygwin, each under
its own license. Below are their Cygwin source packages, with the
license that the package names, where to get their source, the files
bundled from them, and their license files, which are in the folder
usr\share\doc\powwow\licenses\<source package> of the installation
folder.

The CA certificates, etc\pki\tls\certs\ca-bundle.crt, are those of
Mozilla, under the Mozilla Public License 2.0, as the header of
certdata.txt in the licenses of ca-certificates says; the license that
the package names is that of its scripts.
END
    for name in $names; do
        license=$(awk -v n="$name" '$2 == n {
                          $1 = $2 = ""; sub(/^ +/, ""); print; exit }' \
                      "$lists/sources")
        echo
        echo "$name${license:+: $license}"
        echo "    source: https://cygwin.com/packages/summary/$name-src.html"
        awk -v n="$name" '$2 == n { print $1 }' "$lists/sources" \
            | while read -r pkg; do
            awk -v p="$pkg" '$1 == p {
                print "    " $1 " " $2 ": " substr($3, 2) }' "$lists/files"
        done
        (cd "$licenses/$name" && find . -type f | sed 's|^\./||' | sort \
             | sed 's|^|    license: |')
    done
    cat <<'END'

The license files above, with the full texts of the licenses, are in the
folder usr\share\doc\powwow\licenses\<source package> of the installation
folder, such as usr\share\doc\powwow\licenses\gnutls\COPYING.LESSERv2.
END
} > "$stage$prefix/share/powwow/LICENSES.txt"

echo
echo "created $stage"
echo "build the installer on Windows with Inno Setup: make cygwin-installer"
