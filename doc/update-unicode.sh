#! /bin/sh
#
# download the latest Unicode data, and make unicode-names.txt and
# emoji-names.txt again in the directory of this script, with
# unicode-license.txt, the license of the data:
#
#   sh doc/update-unicode.sh
#
# everything is downloaded and made in a temporary directory, and checked,
# before the files are replaced; on any error, they are left as they were

set -eu

url=https://www.unicode.org/Public
doc=$(cd "$(dirname "$0")" && pwd)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp" "$doc/unicode-names.txt.new" "$doc/emoji-names.txt.new" \
          "$doc/unicode-license.txt.new"' EXIT

curl -fsSL -o "$tmp/UnicodeData.txt" "$url/UCD/latest/ucd/UnicodeData.txt"
curl -fsSL -o "$tmp/ReadMe.txt" "$url/UCD/latest/ucd/ReadMe.txt"
curl -fsSL -o "$tmp/emoji-test.txt" "$url/emoji/latest/emoji-test.txt"
curl -fsSL -o "$tmp/unicode-license.txt" https://www.unicode.org/license.txt

ucd_version=$(sed -n 's/.*for version \([0-9][0-9.]*[0-9]\) .*/\1/p' \
                  "$tmp/ReadMe.txt" | head -n 1)
emoji_version=$(sed -n 's/^# Version: \([0-9][0-9.]*[0-9]\)$/\1/p' \
                    "$tmp/emoji-test.txt" | head -n 1)
for v in "$ucd_version" "$emoji_version"; do
    case $v in
        [0-9]*.[0-9]*) ;;
        *) echo "$0: cannot find the Unicode version" >&2; exit 1 ;;
    esac
done

sh "$doc/unicode-names.sh" "$tmp/UnicodeData.txt" "$ucd_version" \
    > "$tmp/unicode-names.txt"
sh "$doc/emoji-names.sh" "$tmp/emoji-test.txt" "$emoji_version" \
    > "$tmp/emoji-names.txt"

# the file $1 must have at least $2 lines, and each of the lines after that
check() {
    file=$1 min=$2
    shift 2
    lines=$(wc -l < "$file")
    if [ "$lines" -lt "$min" ]; then
        echo "$0: $(basename "$file") has only $lines lines" >&2
        exit 1
    fi
    for line in "$@"; do
        if ! grep -Fxq -e "$line" "$file"; then
            echo "$0: $(basename "$file") lacks: $line" >&2
            exit 1
        fi
    done
}
tab=$(printf '\t')
check "$tmp/unicode-names.txt" 30000 \
    "0041${tab}latin capital letter a" "2014${tab}em dash"
check "$tmp/unicode-license.txt" 20 "UNICODE LICENSE V3"
check "$tmp/emoji-names.txt" 3000 \
    "1F44D${tab}thumbs up${tab}People & Body${tab}hand-fingers-closed" \
    "1F1F8 1F1EA${tab}flag: Sweden${tab}Flags${tab}country-flag"

# copy them next to the files first, so that renaming replaces them whole
for f in unicode-names.txt emoji-names.txt unicode-license.txt; do
    cp "$tmp/$f" "$doc/$f.new"
    mv "$doc/$f.new" "$doc/$f"
done
echo "Unicode $ucd_version and emoji $emoji_version names updated in $doc"
