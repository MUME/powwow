#! /bin/sh
#
# download the Cygwin source packages of packages that bundle.sh
# bundles, and put their license files in cygwin-licenses in the current
# directory, the build directory, which bundle.sh copies into the bundle;
# run anywhere with curl, xz, zstd and tar, such as with make
# update-cygwin-licenses:
#
#     cygwin/update-licenses.sh [PACKAGE...]
#
# The PACKAGEs are updated, or added, such as those that bundle.sh finds
# without licenses and updates so; without any, all the packages of
# cygwin-licenses/PACKAGES are. The source packages of their current
# versions are downloaded to cygwin-sources/ in the current directory,
# checked against setup.ini, and kept there for the next run. PACKAGES
# gets, for each package, its source package and the license that the
# source package names; cygwin-licenses/SOURCE the license files of each
# source package, near the top of its source tree.

set -e
# V=1, such as from make V=1, shows the commands as they run
[ "${V:-0}" = 0 ] || set -x

licenses=$(pwd)/cygwin-licenses
cache=$(pwd)/cygwin-sources
work=$cache/work

# GNU tar unpacks the source packages with xz, zstd, bzip2 and lzip,
# which gettext has; bsdtar has them built in
programs="curl xz zstd tar sha512sum"
case $(tar --version 2> /dev/null) in
    *GNU*) programs="$programs bzip2 lzip" ;;
esac
for p in $programs; do
    if ! command -v "$p" > /dev/null; then
        echo "$0: install $p" >&2
        exit 1
    fi
done

# the mirrors to download from, in turn when one fails: $CYGWIN_MIRROR,
# the one that setup last used, as setup.rc has it on the next line, and
# two big ones
mirrors=$({
    echo "${CYGWIN_MIRROR:-}"
    awk '{ sub(/\r$/, "") }
         $0 == "last-mirror" { getline; sub(/^[ \t]+/, ""); sub(/\r$/, "");
                               print; exit }' /etc/setup/setup.rc \
        2> /dev/null || true
    echo https://mirrors.kernel.org/sourceware/cygwin/
    echo https://cygwin.mirror.constant.com/
} | sed -n 's|/*$|/|; /^https*:/p' | awk '!seen[$0]++')

# download the file $1 of the mirrors to the file $2: through $2.part, so
# that a failed download leaves no file, retrying and resuming after
# errors, and from the next mirror if one fails; on Cygwin, by its
# Windows path, which the curl of Windows, found in PATH if Cygwin has
# none, understands too
fetch() {
    part=$2.part
    if command -v cygpath > /dev/null; then
        part=$(cygpath -w "$part")
    fi
    for m in $mirrors; do
        if curl -fsSL --retry 5 --retry-all-errors --retry-delay 3 \
                --connect-timeout 30 -C - -o "$part" "$m$1"; then
            mv "$2.part" "$2"
            return 0
        fi
        echo "$0: cannot download $1 from $m" >&2
        rm -f "$2.part"
    done
    return 1
}

# download the file $1 of the mirrors to the file $2 unless it has the
# SHA-512 $3 already, and check that it does then
download() {
    for try in 1 2; do
        if [ -f "$2" ] \
           && [ "$(sha512sum < "$2" | cut -d ' ' -f 1)" = "$3" ]; then
            return 0
        fi
        rm -f "$2"
        echo "downloading $1"
        fetch "$1" "$2" || return 1
    done
    echo "$0: $1 does not match its SHA-512 in setup.ini" >&2
    return 1
}

if [ $# -gt 0 ]; then
    packages=$(for p in "$@"; do echo "$p"; done | sort -u)
elif [ -f "$licenses/PACKAGES" ]; then
    packages=$(sed -n 's/^\([^#][^ ]*\) .*/\1/p' "$licenses/PACKAGES")
else
    echo "$0: no licenses in $licenses yet: make cygwin-bundle gets" \
         "them, or give the packages" >&2
    exit 1
fi

mkdir -p "$cache" "$licenses"
fetch x86_64/setup.xz "$cache/setup.xz"
xz -dc "$cache/setup.xz" > "$cache/setup.ini"

# the source package of the current version of each package, as
# "PACKAGE SOURCE PATH SHA512"
for pkg in $packages; do
    src=$(awk -v p="$pkg" '
              /^@ / { if (in_pkg) exit; in_pkg = ($2 == p); next }
              in_pkg && /^\[/ { exit }
              in_pkg && /^srcpkg: / { name = $2; sub(/-src$/, "", name) }
              in_pkg && /^source: / { path = $2; sum = $4 }
              END { if (path != "")
                        print (name != "" ? name : "-"), path, sum }' \
              "$cache/setup.ini")
    if [ -z "$src" ]; then
        echo "$0: no package $pkg in setup.ini" >&2
        exit 1
    fi
    set -- $src
    name=$1
    if [ "$name" = - ]; then
        # the name of the file of the source package, without its version
        name=${2##*/}
        name=${name%-*-*-src.tar.*}
    fi
    echo "$pkg $name $2 $3"
done > "$cache/sources"

# download the source packages, unless they are there already
cut -d ' ' -f 3,4 "$cache/sources" | sort -u | while read -r path sum; do
    download "$path" "$cache/${path##*/}" "$sum"
done

# the license files of a source tree, from the paths in an archive on
# standard input: the COPYING, LICENSE and the like near its top, but not
# of tests or examples, and certdata.txt, which has the license of the CA
# certificates in its header
license_files() {
    awk '{
        n = split($0, part, "/")
        base = toupper(part[n])
        if (base == "" || n > 4 || $0 ~ /\/(tests?|testsuite|examples?)\//)
            next
        if (base ~ /^(COPYING|COPYRIGHT|LICEN[CS]E|NOTICE)([._-]|V?[0-9]|$)/ \
            || base ~ /_LICEN[CS]E$/ || base == "CERTDATA.TXT")
            print
    }'
}

# the license files of each source package, and the license it names
rm -f "$cache/names"
cut -d ' ' -f 2,3 "$cache/sources" | sort -u | while read -r name path; do
    # made in $work, and replacing those in cygwin-licenses only if all
    # goes well
    dest=$work/licenses
    rm -rf "$work"
    mkdir -p "$dest"
    if ! tar -xf "$cache/${path##*/}" -C "$work"; then
        echo "$0: cannot unpack ${path##*/}" >&2
        exit 1
    fi
    # the upstream archives in the source package, and its own files
    for a in "$work"/*/*; do
        case $a in
            *.sig|*.asc) ;;
            *.tar.*|*.tgz)
                if ! tar -tf "$a" > "$work/list" \
                   || { license_files < "$work/list" > "$work/members"
                        [ -s "$work/members" ] \
                        && ! tar -xf "$a" -C "$dest" --strip-components 1 \
                                 -T "$work/members"; }; then
                    echo "$0: cannot unpack ${a##*/}" >&2
                    exit 1
                fi
                ;;
            *)
                if echo "${a#"$work"/}" | license_files | grep -q .; then
                    cp "$a" "$dest/"
                fi
                ;;
        esac
    done
    sed -n 's/^LICENSE="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' \
        "$work"/*/*.cygport 2> /dev/null | head -n 1 > "$work/license"
    echo "$name $(cat "$work/license")" >> "$cache/names"
    # only the header of certdata.txt, with its license, is wanted
    find "$dest" -name certdata.txt | while read -r f; do
        sed -n '/^#/!q; p' "$f" > "$f.header"
        rm "$f"
    done
    if [ -z "$(find "$dest" -type f)" ]; then
        echo "$0: warning: no license files in $name" >&2
    fi
    rm -rf "${licenses:?}/$name"
    mv "$dest" "$licenses/$name"
done
rm -rf "$work"

# the table of the packages: those updated, and the others as they were
{
    echo "# PACKAGE SOURCE LICENSE: the Cygwin packages that bundle.sh"
    echo "# bundles, their source packages, whose license files are in"
    echo "# the directories beside this, and the license that each source"
    echo "# package names; made by update-licenses.sh"
    {
        while read -r pkg name path sum; do
            license=$(awk -v n="$name" '$1 == n { $1 = ""; sub(/^ /, "");
                                                  print; exit }' \
                          "$cache/names")
            echo "$pkg $name${license:+ $license}"
        done < "$cache/sources"
        if [ -f "$licenses/PACKAGES" ]; then
            grep -v '^#' "$licenses/PACKAGES" \
                | awk 'NR == FNR { new[$1] = 1; next } !($1 in new)' \
                      "$cache/sources" -
        fi
    } | LC_ALL=C sort           # in the same order in every locale
} > "$cache/PACKAGES"
mv "$cache/PACKAGES" "$licenses/PACKAGES"
rm -f "$cache/names"

# the license files of source packages that no package has any more
for d in "$licenses"/*/; do
    name=$(basename "$d")
    if ! cut -d ' ' -f 2 "$licenses/PACKAGES" | grep -Fxq "$name"; then
        rm -r "$d"
    fi
done
echo "updated $licenses"
