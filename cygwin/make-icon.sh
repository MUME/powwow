#! /bin/sh
#
# make powwow.ico, the placeholder icon of powwow on Windows, in the
# current directory: "POW" and "WOW" in red on a comic book burst,
# over a computer monitor that shows green text on black; needs
# ImageMagick 7 and a heavy font, such as Impact:
#
#     cygwin/make-icon.sh [FONT]
#
# FONT is the path of a font file; the default is Impact of macOS or
# Windows. The installer uses cygwin/powwow.ico, made by running this in
# cygwin/

set -e

font=${1:-}
if [ -z "$font" ]; then
    for f in /System/Library/Fonts/Supplemental/Impact.ttf \
             /cygdrive/c/Windows/Fonts/impact.ttf; do
        if [ -e "$f" ]; then
            font=$f
            break
        fi
    done
fi
if [ -z "$font" ] || [ ! -e "$font" ]; then
    echo "$0: cannot find a font; give the path of one, such as" \
         "Impact" >&2
    exit 1
fi

# the layers, made in the current directory too
trap 'rm -f powwow-icon-back.png powwow-icon-outline.png \
            powwow-icon-fill.png powwow-icon-red.png powwow-icon.png \
            powwow-icon-small.png powwow-icon-24.png powwow-icon-16.png' EXIT

# the points of the burst: 14 spikes, a little uneven
burst=$(awk 'BEGIN {
    pi = atan2(0, -1)
    for (i = 0; i < 28; ++i) {
        r = (i % 2 == 0 ? 98 : 68) * (1 + 0.08 * sin(i * 2.3))
        a = pi * i / 14 - pi / 2
        printf "%.1f,%.1f ", 128 + r * cos(a) * 1.08, 116 + r * sin(a) * 0.92
    }
}')
# the scan lines of the screen
lines=$(awk 'BEGIN { for (y = 30; y < 194; y += 4)
                         printf "line 30,%d 226,%d ", y, y }')

magick -size 256x256 xc:none \
    -fill '#c9c1a6' -stroke '#4a4636' -strokewidth 4 \
    -draw 'roundrectangle 10,10 246,214 22,22' \
    -fill '#8f8872' -stroke none \
    -draw 'polygon 92,214 164,214 182,244 74,244' \
    -fill '#b5ad92' -stroke '#4a4636' -strokewidth 3 \
    -draw 'roundrectangle 58,236 198,250 6,6' \
    -fill '#020a02' -stroke '#2a2a20' -strokewidth 3 \
    -draw 'roundrectangle 26,24 230,196 18,18' \
    \( -size 256x256 xc:none -stroke '#33ff3340' -strokewidth 1 \
       -draw "$lines" \) -composite \
    -fill '#33ff33' -stroke none -font "$font" -pointsize 22 \
    -draw "text 36,52 '> _'" \
    -fill '#ffe11a' -stroke '#111111' -strokewidth 5 \
    -draw "polygon $burst" \
    powwow-icon-back.png

# the letters, on a convex surface whose top is between POW and WOW, at
# (130,136): barrel distortion magnifies its middle, and the light that
# falls on it from the upper left shades their red, brightest at (112,116)
# the shape of the letters, as a mask: letters FILE OPTION...
letters() {
    out=$1
    shift
    magick -size 256x256 xc:none -font "$font" -pointsize 64 \
        -gravity center "$@" \
        -annotate -8x-8+0-22 'POW' -annotate -8x-8+4+38 'WOW' \
        -virtual-pixel transparent -distort Barrel '0 0 0.6 0.4 130 136' \
        -alpha extract "$out"
}
letters powwow-icon-outline.png -fill white -stroke white -strokewidth 6
letters powwow-icon-fill.png -fill white -stroke none
magick -size 256x256 xc:'#e3141b' \
    \( -size 400x400 radial-gradient:'#cccccc'-'#202020' \
       -crop 256x256+88+84 +repage \) -compose hard_light -composite \
    powwow-icon-fill.png -alpha off -compose copy_opacity -composite \
    powwow-icon-red.png
magick powwow-icon-back.png \
    \( -size 256x256 xc:'#111111' powwow-icon-outline.png -alpha off \
       -compose copy_opacity -composite \) -compose over -composite \
    powwow-icon-red.png -compose over -composite \
    powwow-icon.png

# the icon at 32 pixels and less, drawn at 32 pixels: scaled down, the
# letters above blur into the burst, so here they are plain and large,
# with a thin outline, on a burst and a monitor without details
small_burst=$(awk 'BEGIN {
    pi = atan2(0, -1)
    for (i = 0; i < 20; ++i) {
        r = i % 2 == 0 ? 16 : 11
        a = pi * i / 10 - pi / 2
        printf "%.1f,%.1f ", 16 + r * cos(a), 13.5 + r * sin(a) * 0.85
    }
}')
magick -size 32x32 xc:none \
    -fill '#c9c1a6' -stroke '#4a4636' -strokewidth 1 \
    -draw 'roundrectangle 0.5,0.5 31.5,26.5 3,3' \
    -fill '#8f8872' -stroke none -draw 'polygon 12,27 20,27 22,30 10,30' \
    -fill '#b5ad92' -stroke '#4a4636' -strokewidth 1 \
    -draw 'rectangle 7.5,29.5 24.5,31.5' \
    -fill '#020a02' -stroke none -draw 'rectangle 2,2 29,24' \
    -fill '#ffe11a' -stroke '#111111' -strokewidth 1 \
    -draw "polygon $small_burst" \
    -font "$font" -pointsize 11 -gravity center \
    -fill '#e3141b' -stroke '#111111' -strokewidth 2 \
    -annotate +0-8 'POW' -annotate +0+2 'WOW' \
    -stroke none -annotate +0-8 'POW' -annotate +0+2 'WOW' \
    powwow-icon-small.png

# the icon at 24 pixels: two lines do not fit, so only POW, on the screen
burst24=$(awk 'BEGIN {
    pi = atan2(0, -1)
    for (i = 0; i < 16; ++i) {
        r = i % 2 == 0 ? 12 : 8.5
        a = pi * i / 8 - pi / 2
        printf "%.1f,%.1f ", 12 + r * cos(a), 11 + r * sin(a) * 0.85
    }
}')
magick -size 24x24 xc:none \
    -fill '#c9c1a6' -stroke '#4a4636' -strokewidth 1 \
    -draw 'roundrectangle 0.5,0.5 23.5,21.5 2,2' \
    -fill '#020a02' -stroke none -draw 'rectangle 2,2 21,19' \
    -fill '#ffe11a' -stroke '#111111' -strokewidth 1 \
    -draw "polygon $burst24" \
    -font "$font" -pointsize 11 -gravity center \
    -fill '#e3141b' -stroke '#111111' -strokewidth 2 -annotate +0-1 'POW' \
    -stroke none -annotate +0-1 'POW' \
    -fill '#8f8872' -draw 'rectangle 8,22 15,23' \
    powwow-icon-24.png

# the icon at 16 pixels: only the burst, with a P
burst16=$(awk 'BEGIN {
    pi = atan2(0, -1)
    for (i = 0; i < 16; ++i) {
        r = i % 2 == 0 ? 8 : 5.5
        a = pi * i / 8 - pi / 2
        printf "%.1f,%.1f ", 8 + r * cos(a), 8 + r * sin(a)
    }
}')
magick -size 16x16 xc:none \
    -fill '#ffe11a' -stroke '#111111' -strokewidth 1 \
    -draw "polygon $burst16" \
    -font "$font" -pointsize 13 -gravity center \
    -fill '#e3141b' -stroke '#111111' -strokewidth 2 -annotate +0+0 'P' \
    -stroke none -annotate +0+0 'P' \
    powwow-icon-16.png

# the sizes that Windows uses, each scaled from one of them, or drawn at it
magick \( powwow-icon.png -resize 256x256 \) \
    \( powwow-icon.png -resize 128x128 \) \
    \( powwow-icon.png -resize 64x64 \) \
    \( powwow-icon.png -resize 48x48 \) \
    \( powwow-icon-small.png -resize 32x32 \) \
    powwow-icon-24.png powwow-icon-16.png \
    powwow.ico
echo "created powwow.ico"
