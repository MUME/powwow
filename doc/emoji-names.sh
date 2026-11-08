#! /bin/sh
#
# make emoji-names.txt, the emoji names that &insert-emoji looks up, from
# emoji-test.txt of Unicode Technical Standard #51:
#
#   curl -O https://www.unicode.org/Public/emoji/latest/emoji-test.txt
#   sh emoji-names.sh emoji-test.txt VERSION > emoji-names.txt
#
# each line is the code points of an emoji in hex, separated by spaces, a
# tab, its name, a tab, its group, a tab, and its subgroup; only the fully
# qualified emoji are listed

[ $# -eq 2 ] || { echo "usage: $0 emoji-test.txt VERSION" >&2; exit 2; }

echo "# Unicode emoji $2 names, from emoji-test.txt"
awk '
/^# group: / { group = substr($0, 10); next }
/^# subgroup: / { subgroup = substr($0, 13); next }
/; fully-qualified/ {
    seq = $0
    sub(/ *;.*/, "", seq)
    name = $0
    sub(/^[^#]*# [^ ]+ E[0-9.]+ /, "", name)
    print seq "\t" name "\t" group "\t" subgroup
}' "$1"
