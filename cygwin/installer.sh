#! /bin/sh
#
# make a Windows installer of powwow with Inno Setup, from the tree that
# bundle.sh builds; run in Cygwin, in the build directory:
#
#     cygwin/installer.sh DIR VERSION
#
# The installer is written to the current directory. ISCC.exe, the
# compiler of Inno Setup, is $ISCC if set, else found in PATH, in the
# registry or in the folders where Inno Setup installs by default.

set -e

if [ $# -ne 2 ] || [ ! -d "$1" ]; then
    echo "usage: $0 DIR VERSION, where DIR is the tree that bundle.sh" \
         "built" >&2
    exit 1
fi
stage=$1
version=$2
here=$(dirname "$0")

# the folders where Inno Setup, newest version first, says it is
# installed, for all users and for the current user only, and then those
# where it installs by default
inno_dirs() {
    uninstall=Microsoft/Windows/CurrentVersion/Uninstall
    for inno in 7 6; do
        key="$uninstall/Inno Setup ${inno}_is1"
        for root in HKEY_LOCAL_MACHINE/SOFTWARE/WOW6432Node \
                    HKEY_LOCAL_MACHINE/SOFTWARE HKEY_CURRENT_USER/SOFTWARE; do
            value="/proc/registry/$root/$key/InstallLocation"
            if [ -r "$value" ]; then
                cygpath -u "$(tr -d '\0' < "$value")"
            fi
        done
        for var in PROGRAMFILES\(X86\) PROGRAMFILES LOCALAPPDATA; do
            dir=$(printenv "$var" || true)
            if [ -n "$dir" ]; then
                case $var in
                    LOCALAPPDATA) dir="$dir/Programs" ;;
                esac
                echo "$(cygpath -u "$dir")/Inno Setup $inno"
            fi
        done
    done
}

iscc=${ISCC:-}
if [ -z "$iscc" ]; then
    iscc=$(command -v ISCC.exe || command -v iscc || true)
fi
if [ -z "$iscc" ]; then
    iscc=$(inno_dirs | while read -r dir; do
               if [ -x "${dir%/}/ISCC.exe" ]; then
                   echo "${dir%/}/ISCC.exe"
                   break
               fi
           done)
fi
if [ -z "$iscc" ]; then
    echo "$0: cannot find ISCC.exe of Inno Setup; install it, or set" \
         "ISCC to its path" >&2
    exit 1
fi

# absolute paths: ISCC takes relative ones from the folder of the script
"$iscc" /Q "/DStage=$(cygpath -aw "$stage")" "/DVersion=$version" \
    "/O$(cygpath -aw .)" "$(cygpath -aw "$here/powwow.iss")"
