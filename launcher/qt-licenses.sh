#! /bin/sh
#
# the licenses of the modules of Qt in a bundle of the launcher: for each
# SPDX file of a module, MODULE-VERSION.spdx, which lists its parts and
# their licenses, a folder DEST/MODULE with it and the texts of those
# licenses, from the folder LICENSES of the source of the module of the
# same version, which the first run downloads with git to qt-licenses in
# the current directory, the build directory:
#
#     launcher/qt-licenses.sh DEST SPDX...

set -e
# V=1, such as from make V=1, shows the commands as they run
[ "${V:-0}" = 0 ] || set -x

if [ $# -lt 2 ]; then
    echo "usage: $0 DEST SPDX..." >&2
    exit 1
fi
dest=$1
shift
if ! command -v git > /dev/null; then
    echo "$0: install git" >&2
    exit 1
fi

for spdx in "$@"; do
    name=$(basename "$spdx" .spdx)
    module=${name%-*}
    version=${name##*-}
    cache=$(pwd)/qt-licenses/$version
    if [ ! -d "$cache/$module" ]; then
        # the tag fetched, not cloned, which would warn that it is not a
        # commit, as it is annotated
        tmp=$cache/$module.tmp
        rm -rf "$tmp"
        git init -q "$tmp"
        git -C "$tmp" remote add origin "https://github.com/qt/$module.git"
        git -C "$tmp" sparse-checkout set --no-cone /LICENSES/
        git -C "$tmp" fetch -q --depth 1 --filter=blob:none origin \
            tag "v$version"
        git -C "$tmp" checkout -q FETCH_HEAD
        mv "$tmp/LICENSES" "$cache/$module"
        rm -rf "$tmp"
    fi
    mkdir -p "$dest/$module"
    cp "$spdx" "$cache/$module"/* "$dest/$module/"
done
