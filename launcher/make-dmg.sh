#! /bin/sh
#
# make a disk image of the launcher for macOS, powwow-VERSION.dmg, with
# powwow and its data in the app, the libraries of Qt and the others that
# they need, and their licenses; run in the build directory after make and
# make launcher, as make macos-dmg does:
#
#     launcher/make-dmg.sh VERSION
#
# macdeployqt, of the Qt that CMake found for the launcher, copies the
# libraries; the app is signed ad hoc, as macOS needs it to run on Apple
# silicon, but not by a developer that Apple knows.

set -e
# V=1, such as from make V=1, shows the commands as they run
[ "${V:-0}" = 0 ] || set -x

if [ $# -ne 1 ]; then
    echo "usage: $0 VERSION" >&2
    exit 1
fi
version=$1
here=$(cd "$(dirname "$0")" && pwd)

# what it does, as some of it takes minutes
step() {
    echo "  $*"
}
built=launcher/build/powwow-launcher.app
if [ ! -d "$built" ]; then
    echo "$0: no $built; run make launcher first" >&2
    exit 1
fi

# the Qt that CMake found, as PREFIX/lib/cmake/Qt6, and its macdeployqt
qtcmake=$(sed -n 's/^Qt6_DIR:PATH=//p' launcher/build/CMakeCache.txt)
qtprefix=$(cd "$qtcmake/../../.." && pwd)
deploy=$qtprefix/bin/macdeployqt
if [ ! -x "$deploy" ]; then
    deploy=$(command -v macdeployqt || true)
fi
if [ -z "$deploy" ]; then
    echo "$0: cannot find macdeployqt of Qt in $qtprefix/bin or PATH" >&2
    exit 1
fi

# anew, so that nothing stays that an image before had
step "putting powwow in powwow-mac/powwow.app"
stage=$(pwd)/powwow-mac
rm -rf "$stage"
mkdir -p "$stage"
app=$stage/powwow.app
cp -R "$built" "$app"

# powwow next to the launcher, which finds it there, and its data in
# Resources, which powwow finds next to its folder MacOS; quietly, unless
# V=1, as install-strip shows each file that it installs
install=$stage/install
if [ "${V:-0}" = 0 ]; then
    make -s --no-print-directory install-strip DESTDIR="$install" \
        > /dev/null
else
    make install-strip DESTDIR="$install"
fi
cp "$(find "$install" -path '*/bin/powwow' | head -n 1)" "$app/Contents/MacOS/"
mkdir -p "$app/Contents/Resources"
cp "$(find "$install" -type d -path '*/share/powwow' | head -n 1)"/* \
    "$app/Contents/Resources/"
rm -rf "$install"

# the libraries of both in the app, also those of Qt in other folders,
# such as the formulas of Homebrew each in their own; their plugins find
# each other by rpaths that macdeployqt cannot follow, which it reports,
# but those of the app do, as the app runs, so that is shown only with V=1
step "copying the libraries into the app with macdeployqt, which takes" \
     "minutes"
log=$stage/macdeployqt.log
if ! "$deploy" "$app" -qmldir="$here" \
         -executable="$app/Contents/MacOS/powwow" -libpath="$qtprefix/lib" \
         -always-overwrite \
         -verbose="$([ "${V:-0}" = 0 ] && echo 0 || echo 1)" 2> "$log"; then
    cat "$log" >&2
    exit 1
fi
if [ "${V:-0}" = 0 ]; then
    grep -v -e 'Cannot resolve rpath' -e '^ERROR:  using' "$log" >&2 || true
else
    cat "$log" >&2
fi
rm -f "$log"

# out again what macdeployqt copies but the launcher does not use: the
# styles of Qt Quick Controls other than macOS and those that it builds
# on, QML modules that none imports, with their plugins, and plugins of
# Qt for widgets, networks, databases and images other than its icon
step "removing the parts of Qt that the launcher does not use"
qml=$app/Contents/Resources/qml
plugins=$app/Contents/PlugIns
for module in QtQuick/Controls/FluentWinUI3 QtQuick/Controls/Imagine \
              QtQuick/Controls/Material QtQuick/Controls/Universal \
              QtQuick/Controls/Native QtQuick/Controls/designer \
              QtQuick/LocalStorage QtQuick/Particles QtQuick/VectorImage \
              QtQuick/Shapes/DesignHelpers QtQuick/tooling \
              QtQml/DesignSupport QtQml/XmlListModel; do
    for plugin in $(find "$qml/$module" -name qmldir \
                        -exec sed -n 's/^.*plugin \([^ ]*\).*/\1/p' {} +); do
        rm -f "$plugins/quick/lib$plugin.dylib"
    done
    rm -rf "${qml:?}/$module"
done
rm -rf "$plugins/styles" "$plugins/tls" "$plugins/networkinformation" \
       "$plugins/sqldrivers" "$plugins/iconengines"
rm -f "$plugins/imageformats/libqgif.dylib" \
      "$plugins/imageformats/libqjpeg.dylib" \
      "$plugins/imageformats/libqsvg.dylib"

# then the libraries that nothing left links, until none is left over,
# as each may have been the last to link others
frameworks=$app/Contents/Frameworks
links=$stage/links
removed=true
while $removed; do
    { find "$app/Contents/MacOS" "$plugins" -type f
      find "$frameworks" -type f \( -name '*.dylib' \
                                   -o -path '*.framework/Versions/A/Qt*' \)
    } | while read -r binary; do
        # what it links, by the name in Frameworks, not its own, which a
        # library lists first
        name=$(basename "$binary")
        otool -L "$binary" | tail -n +2 \
            | sed -n -e 's|.*/\([^/ ]*\.framework\)/.*|\1|p' \
                     -e 's|^[[:space:]]*@.*/\([^/ ]*\.dylib\) .*|\1|p' \
            | grep -v -x -F -e "$name" -e "$name.framework" || true
    done | sort -u > "$links"
    removed=false
    for library in "$frameworks"/*; do
        if ! grep -q -x -F "$(basename "$library")" "$links"; then
            rm -rf "$library"
            removed=true
        fi
    done
done
rm -f "$links"

# the licenses: of Qt, by module, and of the other libraries, by the
# formula of Homebrew that has them, with its files of licenses, or else
# the text of the license that Homebrew names, from those of Qt
step "adding the licenses of the libraries"
licenses=$app/Contents/Resources/licenses
mkdir -p "$licenses"
"$here/qt-licenses.sh" "$licenses/qt" \
    $(ls "$qtprefix"/sbom/*.spdx "$qtprefix"/share/qt/sbom/*.spdx \
          2> /dev/null)
missing=
formulas=
for lib in "$app"/Contents/Frameworks/*.dylib; do
    name=$(basename "$lib")
    # the formula, not an alias of it in opt, such as libtasn for libtasn1
    kept=$(ls -d "$qtprefix"/opt/*/lib/"$name" 2> /dev/null | head -n 1)
    if [ -z "$kept" ]; then
        missing="$missing $name"
        continue
    fi
    keg=$(cd "$(dirname "$kept")/.." && pwd -P)
    formula=$(basename "$(dirname "$keg")")
    case " $formulas " in
        *" $formula "*) continue ;;
    esac
    formulas="$formulas $formula"
    mkdir -p "$licenses/$formula"
    for file in "$keg"/*; do
        case $(basename "$file" | tr '[:upper:]' '[:lower:]') in
            licen[cs]e*|copying*|copyright*|notice*)
                cp "$file" "$licenses/$formula/" ;;
        esac
    done
    if [ -z "$(ls "$licenses/$formula")" ]; then
        id=$(brew info --json=v2 "$formula" 2> /dev/null \
                 | sed -n 's/.*"license": *"\([^"]*\)".*/\1/p' | head -n 1)
        text=$(ls "$licenses"/qt/*/"$id.txt" 2> /dev/null | head -n 1)
        if [ -n "$text" ]; then
            cp "$text" "$licenses/$formula/"
        else
            rmdir "$licenses/$formula"
            missing="$missing $name"
        fi
    fi
done
if [ -n "$missing" ]; then
    echo "$0: no license found for:$missing" >&2
fi
cat > "$licenses/README.txt" <<END
powwow is under the GNU General Public License, version 2 or later: see
COPYING in the folder Resources.

The launcher is built with Qt, whose libraries, in the folder Frameworks
and the folders of plugins and QML modules, are under the GNU Lesser
General Public License, version 3, and their third-party parts under
their own licenses: see the folder qt, with a folder for each module of
Qt, which has its SPDX file, listing its parts and their licenses, and
the texts of those licenses. The source of Qt is at
https://download.qt.io/official_releases/qt/.

The other libraries in the folder Frameworks are under their own
licenses, which the folder of each package of Homebrew that they come
from has:

$(printf '    %s\n' $formulas)
END

# signed anew, as macdeployqt changes the libraries
step "signing the app, and making the image"
codesign --force --deep --sign - "$app" 2> /dev/null

ln -s /Applications "$stage/Applications"
cp "$app/Contents/Resources/powwow.icns" "$stage/.VolumeIcon.icns"

# the icon of the volume needs the flag of a custom icon on its root,
# kHasCustomIcon in the Finder info, which hdiutil does not take from the
# folder; so it is set on the image mounted writable, then compressed
rw=$stage.dmg
rm -f "$rw" "powwow-$version.dmg"
hdiutil create -quiet -volname powwow -srcfolder "$stage" -format UDRW "$rw"
mount=$stage/mount
hdiutil attach -quiet -nobrowse -mountpoint "$mount" "$rw"
xattr -wx com.apple.FinderInfo \
    0000000000000000040000000000000000000000000000000000000000000000 "$mount"
hdiutil detach -quiet "$mount"
hdiutil convert -quiet "$rw" -format UDZO -o "powwow-$version.dmg"
rm -f "$rw"
echo "created powwow-$version.dmg"
