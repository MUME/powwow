#! /bin/sh
#
# make unicode-names.txt, the character names that &insert-char and \N{...}
# look up, from UnicodeData.txt of the Unicode Character Database:
#
#   curl -O https://www.unicode.org/Public/UCD/latest/ucd/UnicodeData.txt
#   sh unicode-names.sh UnicodeData.txt VERSION > unicode-names.txt
#
# each line is a code point in hex and its name; characters without a name
# of their own, such as controls and CJK ideographs, are left out

[ $# -eq 2 ] || { echo "usage: $0 UnicodeData.txt VERSION" >&2; exit 2; }

echo "# Unicode $2 character names, from UnicodeData.txt"
awk -F';' '$2 !~ /^</ { print $1 " " $2 }' "$1"
