#! /bin/sh
#
# make the icon of the launcher on macOS from that of Windows, with sips
# and iconutil of macOS; the CMake build runs this
#
#     launcher/make-icns.sh ICO ICNS

set -e

ico=$1
icns=$2
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

# the largest image of the ICO, 256 pixels wide, in the sizes of an
# iconset, none larger
sips -s format png "$ico" --out "$dir/icon.png" > /dev/null
mkdir "$dir/powwow.iconset"
for size in 16 32 128 256; do
    sips -z $size $size "$dir/icon.png" \
        --out "$dir/powwow.iconset/icon_${size}x${size}.png" > /dev/null
    double=$((size * 2))
    if [ $double -le 256 ]; then
        sips -z $double $double "$dir/icon.png" \
            --out "$dir/powwow.iconset/icon_${size}x${size}@2x.png" \
            > /dev/null
    fi
done
iconutil -c icns "$dir/powwow.iconset" -o "$icns"
