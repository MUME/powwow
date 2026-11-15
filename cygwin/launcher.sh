#! /bin/sh
#
# build the launcher, in launcher/, with the MinGW build of Qt for
# Windows, in launcher-win; run in Cygwin, in the build directory, as
# make launcher does:
#
#     cygwin/launcher.sh build [QT]
#
# or build it and add it to the tree that bundle.sh builds, in
# DIR/launcher, with the DLLs and QML modules of Qt that it needs and
# their licenses, after bundle.sh, as make windows-bundle does:
#
#     cygwin/launcher.sh bundle DIR [QT]
#
# QT is the folder of Qt for MinGW, such as C:\Qt\6.10.3\mingw_64, as
# aqtinstall or the installer of Qt makes it; by default the newest in
# C:\Qt. CMake, Ninja and MinGW come from the folder Tools of the same
# installation, such as C:\Qt\Tools

set -e
# V=1, such as from make V=1, shows the commands as they run
[ "${V:-0}" = 0 ] || set -x

usage() {
    echo "usage: $0 build [QT]" >&2
    echo "       $0 bundle DIR [QT]" >&2
    exit 1
}
case ${1:-} in
    build)
        [ $# -le 2 ] || usage
        stage=
        qt=${2:-}
        ;;
    bundle)
        [ $# -ge 2 ] && [ $# -le 3 ] || usage
        stage=$(realpath -m "$2")
        qt=${3:-}
        ;;
    *)
        usage
        ;;
esac
if [ -z "$qt" ]; then
    qt=$(ls -d /cygdrive/c/Qt/6.*/mingw_64 2> /dev/null | sort -V \
             | tail -n 1)
fi
if [ -z "$qt" ] || [ ! -d "$qt" ]; then
    echo "$0: cannot find Qt for MinGW; install it, such as with" \
         "aqtinstall in C:\\Qt, or give its folder" >&2
    exit 1
fi
qt=$(cygpath -u "$qt")
version=$(basename "$(dirname "$qt")")
tools=$(cd "$qt/../.." && pwd)/Tools
mingw=$(ls -d "$tools"/mingw*_64 2> /dev/null | sort -V | tail -n 1)
for dir in "$qt/bin" "$mingw/bin" "$tools/CMake_64/bin" "$tools/Ninja"; do
    if [ ! -d "$dir" ]; then
        echo "$0: no $dir; install MinGW, CMake and Ninja of Qt in" \
             "$tools" >&2
        exit 1
    fi
done
# the programs of Windows first, not those of Cygwin, such as its cmake
PATH=$qt/bin:$mingw/bin:$tools/CMake_64/bin:$tools/Ninja:$PATH

here=$(cd "$(dirname "$0")" && pwd)
source=$(cygpath -w "$here/../launcher")
build=$(pwd)/launcher-win
cmake -S "$source" -B "$(cygpath -w "$build")" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(cygpath -w "$qt")"
cmake --build "$(cygpath -w "$build")"
[ -n "$stage" ] || exit 0

# the launcher in a folder of its own, as the DLLs of Qt and of MinGW are
# not those of Cygwin; without the shader compilers of Direct3D, which
# Windows has or the launcher does not need, and translations, which it
# has none of; each file copied is shown only with V=1
mkdir -p "$stage/launcher"
cp "$build/powwow-launcher.exe" "$stage/launcher/"
windeployqt --release --no-translations --no-system-d3d-compiler \
    --no-system-dxc-compiler --no-opengl-sw --qmldir "$source" \
    --verbose "$([ "${V:-0}" = 0 ] && echo 0 || echo 1)" \
    "$(cygpath -w "$stage/launcher/powwow-launcher.exe")"

# the licenses of Qt and of the runtime libraries of MinGW, in the list
# that bundle.sh makes and in its folder of license files
list=$(find "$stage" -name LICENSES.txt | head -n 1)
licenses=$stage/usr/share/doc/powwow/licenses
if [ -z "$list" ] || [ ! -d "$licenses" ]; then
    echo "$0: run bundle.sh first" >&2
    exit 1
fi
mkdir -p "$licenses/mingw-gcc" "$licenses/mingw-w64"
# for each module of Qt, its SPDX file, which lists its parts and their
# licenses, and the texts of those licenses, from the folder LICENSES of
# its source of the same version, which the first run downloads to
# qt-licenses in the build directory
if ! command -v git > /dev/null; then
    echo "$0: install git" >&2
    exit 1
fi
cache=$(pwd)/qt-licenses/$version
for spdx in "$qt"/sbom/*-"$version".spdx; do
    module=$(basename "$spdx" "-$version.spdx")
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
    mkdir -p "$licenses/qt/$module"
    cp "$spdx" "$cache/$module"/* "$licenses/qt/$module/"
done
cp "$mingw/licenses/gcc/COPYING3" "$mingw/licenses/gcc/COPYING.RUNTIME" \
    "$licenses/mingw-gcc/"
cp "$mingw/licenses/mingw-w64/COPYING.MinGW-w64-runtime.txt" \
    "$licenses/mingw-w64/"
cat >> "$list" <<END

The launcher
============

The launcher, launcher\\powwow-launcher.exe, which the shortcuts and the
.powwow files open, is built with Qt $version. The libraries of Qt, the
Qt6*.dll files and the folders of plugins and QML modules in the folder
launcher, are under the GNU Lesser General Public License, version 3,
and their third-party parts under their own licenses: see
usr\\share\\doc\\powwow\\licenses\\qt, with a folder for each module of
Qt, which has its SPDX file, listing its parts and their licenses, and
the texts of those licenses.
The source of Qt is at https://download.qt.io/official_releases/qt/.

The runtime libraries of MinGW-w64 GCC in the folder launcher,
libgcc_s_seh-1.dll and libstdc++-6.dll, are under the GNU General Public
License, version 3, with the GCC Runtime Library Exception: see
usr\\share\\doc\\powwow\\licenses\\mingw-gcc; libwinpthread-1.dll is under
the license of the MinGW-w64 runtime: see
usr\\share\\doc\\powwow\\licenses\\mingw-w64.
END
