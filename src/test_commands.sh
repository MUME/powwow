#!/bin/sh
#
# run powwow commands from standard input, and check what they print
#

POWWOW=${POWWOW:-./powwow}

tmp=$(mktemp -d) || exit 99
trap 'rm -rf "$tmp"' EXIT

HOME=$tmp
TERM=xterm
LC_ALL=C
export HOME TERM LC_ALL
# fill memory when it is allocated and freed, on macOS and with glibc, so
# that using a field that was never set, or freed memory, finds no zeros
MallocPreScribble=1
MallocScribble=1
MALLOC_PERTURB_=170
export MallocPreScribble MallocScribble MALLOC_PERTURB_
unset POWWOWDIR POWWOWHELP
POWWOWHELP=${srcdir:-.}/../doc
POWWOWPAGER=cat
export POWWOWHELP POWWOWPAGER

esc=$(printf '\033')
cr=$(printf '\r')
failed=0

fail() {
    echo "FAIL $name: $1"
    sed 's/^/    /' "$tmp/out"
    failed=1
}

# wait until powwow has printed the text $1, for at most 5 seconds
wait_for() {
    i=0
    until grep -Fq -e "$1" "$tmp/raw" 2>/dev/null || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
}

# wait until powwow has run everything sent so far, by sending a command
# that prints the text "<sync$1>"; the input line must be empty
sync_powwow() {
    printf '#print ("<syn" + "c%s>")\n' "$1"
    wait_for "<sync$1>"
}

# start test $1, running the commands in $2; then, if given, type the keys in
# $3 once powwow has run them, and then the keys in $4; the keys must leave
# the input line empty, and the commands sent to wait for powwow are in the
# history
#
# options: --bind when $2 ends with a #bind that reads the key $3,
# --until TEXT to wait for powwow to print TEXT before quitting, and
# --args ARGS to give powwow the arguments ARGS, split at spaces
run() {
    bind= until= args=
    while :; do
        case $1 in
            --args) args=$2; shift 2 ;;
            --bind) bind=1; shift ;;
            --until) until=$2; shift 2 ;;
            *) break ;;
        esac
    done
    name=$1
    : > "$tmp/raw"
    {
        printf '%s\n' "$2"
        if [ $# -gt 2 ]; then
            if [ -n "$bind" ]; then
                wait_for '#please press the key'
                printf '%s' "$3"
                wait_for '#new key binding'
            else
                sync_powwow 1
                printf '%s' "$3"
            fi
            sync_powwow 2
            if [ $# -gt 3 ]; then
                printf '%s' "$4"
                sync_powwow 3
            fi
        fi
        [ -z "$until" ] || wait_for "$until"
        printf '#quit\n'
    } | "$POWWOW" $args > "$tmp/raw" 2>&1 &
    pid=$!
    (sleep 20; kill -9 $pid) > /dev/null 2>&1 &
    watchdog=$!
    wait $pid
    rc=$?
    kill $watchdog 2>/dev/null
    wait $watchdog 2>/dev/null
    # ESC[0m and ESC[m both reset the attributes; termcaps use either
    sed -e "s/$esc\[K//g" -e "s/$cr//g" -e "s/$esc\[0m/$esc[m/g" \
        -e "s/$esc/<ESC>/g" "$tmp/raw" > "$tmp/out"
    [ $rc -eq 0 ] || fail "exit status $rc"
}

# the output must have a line that is exactly $1
has() {
    joined | grep -Fxq -e "$1" || fail "missing line: $1"
}

# the output must not have a line that is exactly $1
lacks() {
    joined | grep -Fxq -e "$1" && fail "unexpected line: $1"
}

# the output, and again with the lines of wrapped messages joined, which
# continue on lines that start with "#    ", or "    " for #ver
joined() {
    cat "$tmp/out"
    awk 'NR > 1 && /^#    / { line = line " " substr($0, 6); next }
         NR > 1 && /^    [^ ]/ { line = line " " substr($0, 5); next }
         NR > 1 { print line }
         { line = $0 }
         END { if (NR > 0) print line }' "$tmp/out"
}

run 'var deletes an unnamed number variable' '#($3 = "text")
#(@3 = 5)
#var @3=
#print ($3)
#print ("@3=" + %(@3))'
has 'text'
has '@3=0'

run 'action reports errors in its pattern expression' '#action >e (1+)=x
#action >f (1)=x'
has '#action: #error: value expected.'
has '#action: #error: string expected.'

run 'prompt with a group' '#prompt >q@c bar=#print ("prompt")
#action >p@a foo=#print ("action")
#prompt >p@b foo=#print ("prompt")
#group a off
#emulate foo
#print ("alive")'
lacks 'action'
has 'alive'

run 'prompt is deleted' '#prompt >q bar=#print ("prompt")
#prompt <q
#print ("alive")'
has '#deleting prompt: >+q bar'
has 'alive'

if grep -q 'define USE_REGEXP 1' ../config.h 2>/dev/null; then
    run 'anonymous action with the pattern of a regexp action' \
        '#action %re ^foo=#print ("one")
#action ^foo=#print ("two")
#emulate foo
#print ("alive")'
    has 'two'
    has 'alive'
fi

run 'bind tells a new key from a redefined one' '#bind zzz ^X^Z=#print ("a")
#bind zzz ^X^Z=#print ("b")'
has '#new key binding: zzz ^X^Z=#print ("a")'
has '#redefined key: zzz ^X^Z=#print ("b")'

run 'aliases and variables are listed in ASCII order' '#alias b=2
#alias c=3
#alias a=1
#alias
#($zb="2")
#($za="1")
#var'
order=$(sed -n -e 's/^#alias \([abc]\)@\*=.*/\1/p' \
               -e 's/^#(\$\(z[ab]\) = .*/\1/p' "$tmp/out" | tr -d '\n')
[ "$order" = abczazb ] || fail "listed in the order $order"

run 'delays are listed in the order of their time' '#in a (50000) #print ("a")
#in b (10000) #print ("b")
#in c (30000) #print ("c")
#in'
order=$(sed -n 's/^#at ([0-9:]*) #in ([0-9]*) "\([abc]\)".*/\1/p' \
            "$tmp/out" | tr -d '\n')
[ "$order" = bca ] || fail "listed in the order $order"

run 'an instruction for an unknown connection' '##nosuch hello
{#print ("in a block")
#print ("alive")'
has '#no connection named "nosuch"'
has '#error: missing right parenthesis.'
has 'alive'

run 'help copyright shows all of COPYING, with and without the pager' \
    '#help copyright
#help -P copyright'
[ "$(grep -c 'is copyrighted by the$' "$tmp/out")" -eq 2 ] \
    || fail "missing the first line of COPYING"
[ "$(grep -c '^Public License instead of this License\.$' "$tmp/out")" \
      -eq 2 ] || fail "missing the last line of COPYING"
[ "$(grep -c '^UNICODE LICENSE V3$' "$tmp/out")" -eq 2 ] \
    || fail "missing the license of the Unicode data"

POWWOWSHELL='printf "[%s]\n"'
export POWWOWSHELL
run 'the shell of #! gets the command as one argument with POWWOWSHELL' \
    "#! echo it's a | b"
# the terminal is reset before the command, on the same line
grep -Fq "[echo it's a | b]" "$tmp/out" || fail "expected the command"
unset POWWOWSHELL

POWWOWSHELL='printf "<%s>\r\n"'
export POWWOWSHELL
run '#var $x=! runs POWWOWSHELL too, and drops the CR of CR LF' '#var $v=!a b
#print ("[" + $v + "]")'
has '[<a b>'
grep -q "<a b>$cr" "$tmp/raw" && fail "the CR was kept"
unset POWWOWSHELL

POWWOWSHELL='printf "{%s}\n"'
export POWWOWSHELL
run '#option shell overrides POWWOWSHELL until cleared' '#option shell=printf "[%s]\n"
#! one
#option shell=
#! two'
grep -Fq '[one]' "$tmp/out" || fail "expected [one]"
grep -Fq '{two}' "$tmp/out" || fail "expected {two}"
unset POWWOWSHELL

run '#save saves #option editor as typed' "#file =$tmp/options.pw
#option editor=my ed\\;it -x
#save"
grep -Fxq '#option editor=my ed\;it -x' "$tmp/options.pw" \
    || fail "expected #option editor in the file"

run 'Tab completes #option editor= without a space' '' "#option ed	vi
"
has '#option editor is now: vi'

run '#! needs no space before its command, but # ! is a comment' \
    '#!echo it works
# !echo not a command'
grep -Fq ">it works" "$tmp/out" || fail "expected it works"
grep -Fq '>not a command' "$tmp/out" && fail "ran the comment"

run '#! runs the command with sh without POWWOWSHELL' "#! echo it\\'s a | tr a b"
grep -Fq ">it's b" "$tmp/out" || fail "expected it's b"

run 'without UTF-8, the copyright sign is shown as (c) and dashes as -' \
    '#print ("x" + *169 + *8211 + *8212 + "y")'
grep -Fq 'x(c)--y' "$tmp/out" || fail "expected x(c)--y"

run '#group lists the aliases and actions of a group' '#alias hi@g1=#print hi
#alias other=#print x
#action >+auto@g1 ZBLAM!=stand
#group g1 off
#group g1 list
#group nosuch list'
has '#alias (disabled) hi@g1=#print hi'
has '#action >-auto@g1 ZBLAM!=stand'
lacks '#alias other@*=#print x'
has '#no aliases or actions are in group nosuch.'

run '#var takes an expression for the number, and spaces around =' \
    '#var @(-1*4) = 27
#print (@-4)
#var $test = a long text
#print ($test)'
has '27'
has 'a long text'

run 'typing does not draw the character before the cursor again' '' \
    "xyz$(printf '\025')"
grep -Fq 'xyz' "$tmp/out" || fail "xyz not shown"
! grep -Eq 'xyz<ESC>\[[0-9;]*H[xyz]' "$tmp/out" \
    || fail "drew the typed characters again"

run 'Tab does not draw the start of the word that it completes again' '' \
    "#hilite mess	$(printf '\025')"
grep -Fq 'ages ' "$tmp/out" || fail "messages not completed"
[ "$(grep -o 'mess' "$tmp/out" | wc -l)" -eq 1 ] \
    || fail "drew the start of the word again"

run 'substitute define, list and delete' '#substitute foo=bar
#substitute
#print ("a foo b foo")
#substitute foo=
#substitute'
has '#new substitution: foo=bar'
has '#substitute foo=bar'
has 'a bar b bar'
has '#deleting substitution: foo=bar'
has '#no substitutions are defined.'

run 'substitute show one' '#substitute foo=bar
#substitute foo

#substitute nope'
has '#changed substitution: foo=bar'
has '#unknown substitution, cannot show: "nope"'

run 'substitute anchor' '#substitute ^foo=bar
#print ("xfoo foo")
#print ("foo foo")'
has 'xfoo foo'
has 'bar foo'

run 'substitute wildcards' '#substitute <&>=T
#print ("a <b c> d")'
has 'a T d'

run 'substitute invalid patterns' '#substitute ^=z
#substitute =z
#substitute $&=z'
has '#substitute must be non-empty.'
has '#error: two wildcards (& or $) may not be next to eachother'
lacks '#new substitution: ^=z'

run 'substitute escapes' '#substitute foo=a\;b\\c\(d\)\=e
#print ("foo")'
has '#new substitution: foo=a\;b\\c\(d\)\=e'
has 'a;b\c(d)=e'

run 'substitute save and load' "#substitute foo=a\;b
#substitute \\^lit=c
#substitute ^beg= d
#save $tmp/cfg
#reset substitute
#substitute
#load $tmp/cfg
#substitute"
has '#no substitutions are defined.'
has '#substitute foo=a\;b'
has '#substitute \^lit=c'
has '#substitute ^beg= d'

run '#load keeps a large #nice' "#nice 300
#save $tmp/cfg
#load $tmp/cfg
#nice"
has '#nice: 300'

run 'substitute before mark' '#substitute foo=bar
#mark bar=bold
#print ("foo")'
has '<ESC>[1mbar<ESC>[m'

run 'substitute keeps the original for actions' '#substitute foo=bar
#action >+a foo=#print ("right")
#action >+b bar=#print ("wrong")
#emulate foo'
has 'right'
lacks 'wrong'

run 'reset substitute' '#substitute foo=bar
#reset substitute
#print ("foo")'
has 'foo'

run 'mark anchor' '#mark ^foo=bold
#print ("xfoo foo")
#print ("foo x")'
has 'xfoo foo'
has '<ESC>[1mfoo<ESC>[m x'

run 'mark the first word' '#mark ^$=bold
#print ("foo x bar")'
has '<ESC>[1mfoo<ESC>[m x bar'

run 'mark with 256 and RGB colors' '#mark foo=bold 208 on #202020
#mark bar=italic strike on 22
#mark
#print ("foo bar")'
has '#new mark: foo=bold 208 on #202020'
has '#mark foo=bold 208 on #202020'
has '#mark bar=italic strike on 22'
has '<ESC>[1;38;5;208;48;2;32;32;32mfoo<ESC>[m <ESC>[3;9;48;5;22mbar<ESC>[m'

run 'mark with an invalid color' '#mark foo=#ff00
#mark foo=256
#mark'
has '#mark: "#ff00" is not a color: write #rrggbb in hex.'
has '#mark: 256 is not a color: numbered colors go from 0 to 255.'
has '#no markers are defined.'

run 'hilite with an invalid attribute' '#hilite ?
#hilite red blue
#hilite purple'
has '#hilite: "?" is not a style or a color.'
has '#hilite: "blue" follows the color: write "on" before a background color.'
has '#hilite: "purple" is not a style or a color.'
has '#syntax: #hilite [input|messages] [<attribute>]'
has '  example:  bold yellow on blue'

run 'bright colors with a capital first letter' '#mark foo=White on BLUE
#mark bar=bold Red
#mark'
has '#mark foo=White on Blue'
has '#mark bar=bold Red'

run 'attr operator' '#print (attr "dim #ff8700" + "x")'
has '<ESC>[2;38;2;255;135;0mx'

run 'substitute the first word' '#substitute ^$=Z
#print ("foo x bar")'
has 'Z x bar'

run 'emulate without a connection' '#emulate hello'
has 'hello'

run --bind 'bind reads a whole key sequence' '#bind foo=bar' \
    "$esc[20~"
has '#new key binding: foo ^[[20~=bar'

run 'word operators skip trailing delimiters' '#print ("foo bar  baz  " >: 1)
#print ("foo bar  baz  " <: 2)
#print ("foo bar  baz  " >: 2)'
has 'baz  '
has 'foo bar '
has 'bar  baz  '

run '#delim does not split command arguments' '#delim program
#host foo-bar.example.com 4242 --no-tls'
has '#host set to: foo-bar.example.com 4242 --no-tls --utf8'

run 'NUL is not a word delimiter' '#($a = "a  \000  b")
#print (:?$a)'
has '3'

run --bind 'transpose words at the end of a line' \
    '#bind foo=&transpose-words' \
    "$esc[20~" "#print foo bar $esc[20~
"
has 'bar foo '

run 'change case at the end of a line' '' \
    "#print foo bar ${esc}u${esc}l${esc}c
"
has 'foo bar '

run 'upcase words and move past them' '' \
    "#print foo bar baz${esc}b${esc}b${esc}u${esc}u!
"
has 'foo BAR BAZ!'

run 'upcase the rest of a word' '' \
    "#print foo barbaz$(printf '\002\002\002')${esc}u!
"
has 'foo barBAZ!'

run 'capitalize and downcase words' '' \
    "#print fOO bAR bAZ${esc}b${esc}b${esc}b${esc}c${esc}l${esc}c!
"
has 'Foo bar Baz!'

run 'C-w deletes back to a space and M-q puts the line in history' '' \
    "#print one${esc}q#print foo  bar-baz $(printf '\027')qux
"
has 'foo  qux'
lacks 'one'

run 'Delete deletes the character under the cursor' '' \
    "#print abc$(printf '\002\002')${esc}[3~
"
has 'ac'

run 'unbound meta and function keys are ignored' '' \
    "#print a${esc}6b${esc}[1;5Cc${esc}OPd${esc}${esc}xe$(printf '\030')zf
"
has 'abcdef'

run 'a key sequence and C-h lists its bindings' '' \
    "$(printf '\0308\b')#print x
"
has '#key bindings starting with C-x 8:'
has '#bind C-x-8-Ret ^X8^M=&insert-char'
has 'x'

run 'Home and End move to the start and end of the line' '' \
    "bc${esc}OH#print a${esc}OFd
"
has 'abcd'

run 'M-t swaps the word at the cursor with the one before' '' \
    "#print one two three$(printf '\002\002')${esc}t!
"
has 'one three two!'

run 'M-. inserts the last word of the previous lines' '' \
    "#print one two
#print x three
#print ${esc}.${esc}.!
"
has 'two!'

cu=$(printf '\037')
run 'C-_ undoes typing and kills, and M-C-_ redoes' '' \
    "#print one${esc}btwo ${cu}
#print a b$(printf '\027')${cu}${esc}${cu}c
"
has 'one'
has 'a c'

run 'consecutive kills are yanked together' '' \
    "#print foo bar${esc}$(printf '\b')${esc}$(printf '\b')x $(printf '\031')
"
has 'x foo bar'

run 'M-y replaces the yanked text with the kill before it' '' \
    "#print X aaa bbb${esc}$(printf '\b\002')${esc}$(printf '\b\005\031')${esc}y
"
has 'X  bbb'

run 'C-u kills to the start of the line' '' \
    "#print abc$(printf '\025')#print z $(printf '\031')
"
has 'z #print abc'

cr=$(printf '\022')
cs=$(printf '\023')
ce=$(printf '\005')
history='#print apple one
#print banana
#print apple two'

run 'C-r finds the last line with the text' "$history" \
    "${cr}ban${ce}s
"
has 'bananas'

run 'C-r again finds an older line, and C-s a newer one' "$history" \
    "${cr}apple${cr}${ce}!
" "${cr}apple${cr}${cr}${cs}${ce}?
"
has 'apple one!'
has 'apple two?'

run 'C-g restores the line, and C-r C-r repeats the last search' \
    "$history" "#print typed${cr}ban$(printf '\007')
" "${cr}${cr}${ce}!
"
has 'typed'
has 'banana!'

run 'M-< and M-> go to the first history line and back' "$history" \
    "#print typed${esc}<${esc}>!
" "${esc}<!
"
has 'typed!'
has 'apple one!'

run 'C-_ undoes moving in the history and searching' "$history" \
    "#print typed$(printf '\020')${cu}!
" "#print typed${cr}ban${ce}${cu}?
"
has 'typed!'
has 'typed?'

run 'C-n after a search goes on to the next line' "$history" \
    "#print typed${cr}ban${ce}$(printf '\016')!
"
has 'apple two!'

run 'BS deletes from the search text' "$history" \
    "${cr}appz$(printf '\b')le${cr}${ce}!
"
has 'apple one!'

utf8=$(locale -a 2>/dev/null | grep -iE '^(C|en_US)\.utf-?8$' | head -n 1)
if [ -n "$utf8" ]; then
    LC_ALL=$utf8
    run 'capitalize and downcase non-ASCII words' '' \
        "#print ǆUNGLA ΟΔΟΣ 12ab${esc}b${esc}b${esc}b${esc}c${esc}c${esc}c!
"
    has 'ǅungla Οδος 12Ab!'

    # C-b and C-f draw the e first, so that the accent joins its cell
    run 'a combining character typed after its base draws them again' '' \
        "$(printf 'e\002\006\314\201\025')"
    grep -Eq "<ESC>\\[[0-9;]*He$(printf '\314\201')" "$tmp/out" \
        || fail "e and the accent not drawn again together"

    run 'version with a copyright sign and an en dash' '#ver'
    joined | grep -q ', © 1998–[0-9]* by the powwow authors$' \
        || fail "expected © and an en dash"

    cx8=$(printf '\0308\r')
    run 'C-x 8 inserts a character by name or code point' '' \
        "#print a${cx8}em dash
b${cx8}latin small letter e with acu	
${cx8}u+263a
${cx8}nosuch
$(printf '\007')c
"
    has 'a—bé☺c'

    run 'the character name can be edited' '' \
        "#print a${cx8}dash$(printf '\001\001')em $(printf '\005')x$(printf '\177')

"
    has 'a—'

    run 'Ret inserts the character whose words the words typed abbreviate' \
        '' "#print a${cx8}lat sma let dz
${cx8}lat sma let
$(printf '\007')b
"
    has 'aǳb'

    run 'Tab completes character names by the starts of their words' '' \
        "#print a${cx8}gr sm al	
${cx8}sm face		$(printf '\007')
"
    has 'aα'
    grep -q '^smirking face  *😏$' "$tmp/out" || fail 'no smirking face'

    run 'Tab keeps the words typed that the common start does not have' '' \
        "#print a${cx8}latin liga	 st

"
    has 'aﬆ'

    # Tab: "latin ligature ", "latin  letter aa" with the cursor in the
    # gap, "latin small l", and "greek letter alpha"
    run 'Tab completes each word, and goes to where the names differ' '' \
        "#print a${cx8}lat ligat	st
${cx8}lati letter aa	s
${cx8}lat sm l	igature ff
${cx8}gr alpha	$(printf '\001')${esc}f small
b
"
    has 'aﬆꜳﬀαb'

    run 'Tab lists the names right after completing part of the name' '' \
        "${cx8}lat lig oe		$(printf '\007')
"
    grep -q '^latin capital ligature oe  *Œ$' "$tmp/out" \
        || fail 'no latin capital ligature oe'

    run 'C-_ undoes editing the character name, and inserting it' '' \
        "#print a${cx8}em dash$(printf '\027')${cu}
${cu}b
"
    has 'ab'

    cx8e=$(printf '\0308ee')
    run 'C-x 8 e e inserts an emoji by name' '' \
        "#print a${cx8e}thumbs up
${cx8e}flag swe
${cx8e}food pizza
b
"
    has 'a👍🇸🇪🍕b'

    # as installed with zlib
    if grep -q 'define HAVE_ZLIB 1' ../config.h 2>/dev/null; then
        mkdir "$tmp/gz"
        cp "$POWWOWHELP/powwow.help" "$tmp/gz"
        for f in unicode-names.txt emoji-names.txt; do
            gzip -c "$POWWOWHELP/$f" > "$tmp/gz/$f.gz"
        done
        help=$POWWOWHELP
        POWWOWHELP=$tmp/gz
        run 'the names can be read compressed by gzip' '' \
            "#print a${cx8}em dash
${cx8e}thumbs up
b
"
        POWWOWHELP=$help
        has 'a—👍b'
    fi

    run 'Tab lists the group of the emoji that match with it' '' \
        "${cx8e}fant		$(printf '\007')
"
    grep -q '^elf (People & Body, person-fantasy)  *🧝$' "$tmp/out" ||
        fail 'no elf (People & Body, person-fantasy)'

    run 'Tab keeps the words of the group, and completes the name' '' \
        "#print a${cx8e}person-fant man mag	

"
    has 'a🧙‍♂️'
    grep -Eq 'person-fant man mag(<ESC>\[[0-9;]*H)?e' "$tmp/out" \
        || fail 'person-fant man mag is not completed to person-fant man mage'

    run 'emoji can be browsed by group and subgroup' '' \
        "${cx8e}		$(printf '\007')
#print a${cx8e}peo/fant	man mag	
b
"
    grep -q '^Flags/ ([0-9]* emoji)$' "$tmp/out" || fail "no list of groups"
    has 'a🧙‍♂️b'

    run 'Tab keeps the separators typed after the last word' '' \
        "#print a${cx8e}peop/fam/man, 	woman, boy, boy
b
"
    has 'a👨‍👩‍👦‍👦b'

    zwsp=$(printf '\342\200\213')
    run 'the cursor moves over a zero width space with the character before' \
        '' "#print x${cx8}zero width space
y$(printf '\002\002')z
"
    has "zx${zwsp}y"
    # where the terminal puts the cursor after it does not matter
    grep -Eq "x${zwsp}$(printf '\033')\\[[0-9]+;[0-9]+H" "$tmp/raw" \
        || fail 'the cursor is not moved after the zero width space'

    run 'Ret inserts the only character whose name matches' '' \
        "#print a${cx8}lat let yr

"
    has 'aƦ'

    run 'Tab lists the best names first' '' \
        "${cx8}en 		$(printf '\007')
"
    sed -n '/^en dash  /,$p' "$tmp/out" | sed -n '2,4s/  .*//p' > "$tmp/first"
    [ "$(cat "$tmp/first")" = "en quad
en space
envelope" ] || fail "en dash, en quad, en space and envelope are not first"

    run 'escapes insert characters by name or code point' \
        '#print ("x\N{em dash}y \u{e9} \N{U+263A} \N{nosuch}")'
    has 'x—y é ☺ N{nosuch}'
    LC_ALL=C
fi

# a multibyte charset other than UTF-8, in which \201 starts a character
# that the space cannot end
gb18030=$(locale -a 2>/dev/null | grep -iE '^zh_CN\.gb18030$' | head -n 1)
if [ -n "$gb18030" ]; then
    LC_ALL=$gb18030
    run 'keys after an invalid character are read' '' \
        "$(printf '#print a\201 b')
"
    has 'a b'
    LC_ALL=C
fi

run '#while needs a parenthesis' '#while 1
#while (
#print (
#while (@1<3) {#print (@1);#(\@1++)}'
has '#while: #error: missing left parenthesis.'
has '#while: #error: value expected.'
has '#print: #error: value expected.'
lacks '#while: #error: math stack underflow.'
has '2'

run '#var numbers' '#var @x=-5
#print (@x)
#var @y=99999999999999999999
#print (@y)
#var @z=abc
#print (@z)'
has '-5'
has '0'
has '#var: #error: operand or index out of range.'
lacks '99999999999999999999'

run --bind 'rebind rejects an empty key sequence' \
    '#bind foo=bar' "$esc[20~" "#rebind foo =
"
has '#rebind: empty key sequence.'

if [ "$(getconf LONG_BIT)" -ge 64 ]; then
    run '#var numbers beyond int' '#var @x=3000000000
#print (@x)
#var @y=-3000000000
#print (@y)'
    has '3000000000'
    has '-3000000000'
fi

run 'numbered variables from -50 to 9' \
    '#(@-50 = 7, @9 = 9, $-50 = "a50", $9 = "n9")
#var @9=-3
#var $-50=b50
#print (%(@-50) + " " + %(@9) + " " + $-50 + " " + $9)
#var
#(@-51 = 1)
#(@10 = 1)
#print ($-51)
#var @-51=3
#var $10=x'
has '7 -3 b50 n9'
has '#(@-50 = 7)'
has '#(@9 = -3)'
has '#($-50 = "b50")'
has '#($9 = "n9")'
has '#print: #error: operand or index out of range.'
[ "$(grep -c '^#error: operand or index out of range\.$' "$tmp/out")" = 4 ] \
    || fail 'expected 4 errors for @-51, @10, @-51 and $10'

run 'nested aliases and actions have their own parameters' \
    '#alias in={#(@(1) = 7);#print ("in "+$(0)+"|"+$(1)+"|"+$(2)+"|"+%(@(1)))}
#alias out={#(@(1)=3);in x;#print ("o "+$(0)+"|"+$(1)+"|"+$(2)+"|"+%(@(1)))}
#action >+hit ^hit &1={#(@(1) = 4);out a b;#print ("hit "+$(0)+"|"+%(@(1)))}
#emulate hit foo'
has 'in x|x||7'
has 'o a b|a|b|3'
has 'hit hit foo|4'

run 'the parameters outside of any call are kept' \
    '#($1 = "top", @1 = 5)
#alias noop=#print ("noop")
noop
#for (@(2) = 0; @(2) < 1; @(2)++) #print ("for")
#print ("after "+$(1)+%(@(1)))'
has 'after top5'

run 'deleting a named variable keeps the others' \
    '#(@a = 1, @b = 2, @c = 3, $x = "x", $y = "y", $z = "z")
#var @a=
#var $x=
#print (%(@b) + %(@c) + $y + $z)
#(@b += 10, @c += 20, $y += "Y", $z += "Z", @d = 4, $w = "w")
#print (%(@b) + " " + %(@c) + " " + %(@d) + " " + $w + $y + $z)
#var'
has '23yz'
has '12 23 4 wyYzZ'
has '#(@b = 12)'
has '#(@d = 4)'
has '#($y = "yY")'
has '#($w = "w")'
lacks '#(@a = 1)'
lacks '#($x = "x")'

run '@(name) and $(name) are the named variables' \
    '#($0 = "zero", $foo = "FOO", @foobar = 5, $1 = "foo")
#print ("1 "+$("foo"))
#print (@("foo"+"bar"))
#($("foo") = "bar")
#print ("2 "+$foo+$0)
#($$1 += "!")
#print ("3 "+$foo)'
has '1 FOO'
has '5'
has '2 barzero'
has '3 bar!'

run 'repeating the text of a variable leaves the variable alone' \
    '#($s = "b", $b = "c")
#print ("1 " + ($$s * 0) + ($$s * 2) + ($s * 0))
#print ("2 " + $s + $b)
#print ("3 " + ($("a" < "b") * 0))'
has '1 cc'
has '2 bc'
has '3 '

run 'dividing by zero is an error' \
    '#(@a = 7)
#(@a /= 0)
#(@a %= 0)
#print (7 / 0)
#print (7 % 0)
#print ("a=" + %@a)'
[ "$(grep -c '#error: division by zero\.$' "$tmp/out")" = 4 ] \
    || fail 'expected 4 errors for /=, %=, / and %'
has 'a=7'

# MAX_STACK is 100: 100 operators "(" or values "@a" fit on the stacks
open=$(printf '%.0s(' $(seq 100)) close=$(printf '%.0s)' $(seq 100))
assign=$(printf '%.0s@a=' $(seq 99))
run 'an expression may fill the stacks but not overflow them' \
    "#print ($open 1 $close)
#print (($open 2 $close))
#print (${assign}3)
#print (${assign}@a=4)"
has '1'
lacks '2'
has '3'
lacks '4'
[ "$(grep -c '^#print: #error: math stack overflow\.$' "$tmp/out")" = 2 ] \
    || fail 'expected 2 stack overflow errors'

run '^^ gives 0 or 1' \
    '#print ("xor " + %(5 ^^ 0) + %(0 ^^ 7) + %(5 ^^ 3) + %(0 ^^ 0))'
has 'xor 1100'

run 'digits of a number must be less than its base' \
    '#print ("n " + %(2#101) + " " + %(36#Zz) + " " + %(#fF))
#print ("chained " + %(3#12#100))
#print (2#102)
#print (8#9)
#print (10#a)
#print (99999999999999999999)
#print (#fffffffffffffffff)'
has 'n 5 1295 255'
has 'chained 25'
[ "$(grep -c '^#print: #error: invalid digit in numeric value\.$' \
    "$tmp/out")" = 3 ] || fail 'expected 3 errors for invalid digits'
[ "$(grep -c '^#print: #error: operand or index out of range\.$' \
    "$tmp/out")" = 2 ] || fail 'expected 2 errors for too large numbers'

run 'shifts lose the bits shifted out and keep the sign' \
    '#print ("l " + %(3 << 2) + " " + %(1 << 64) + " " + %(1 << 1000))
#print ("r " + %(-8 >> 1) + " " + %(-1 >> 64) + " " + %(5 >> 64))
#(@a = 3, @b = -5)
#(@a <<= 70, @b >>= 70)
#print ("v " + %@a + " " + %@b)
#print (1 << -1)
#print (1 >> -1)
#(@b <<= -1)
#print ("b " + %@b)'
has 'l 12 0 0'
has 'r -4 -1 0'
has 'v 0 -1'
has 'b -1'
[ "$(grep -c '#error: operand or index out of range\.$' "$tmp/out")" = 3 ] \
    || fail 'expected 3 errors for negative shifts'

if [ "$(getconf LONG_BIT)" -eq 64 ]; then
    run 'the smallest number divided by -1 wraps around' \
        '#(@m = -9223372036854775807 - 1, @a = @m, @b = @m)
#(@a /= -1, @b %= -1)
#print ("m " + %(@m / -1) + " " + %(@m % -1) + " " + %@a + " " + %@b)'
    has 'm -9223372036854775808 0 -9223372036854775808 0'
fi

run '% gives a non-negative remainder' \
    '#(@a = -7)
#(@a %= -3)
#print ("r " + %(-7 % -3) + " " + %(7 % -3) + " " + %(-7 % 3) + " " + %@a)'
has 'r 2 1 2 2'

run 'an expression assigns to a variable that it reads' \
    '#(@foo = 5)
#(@foo = @foo + 1)
#(@foo += @foo)
#print (@foo)
#($foo = "ab")
#($foo += $foo)
#print ($foo)
#($foo = $foo + "c" + $foo)
#print ($foo)'
has '12'
has 'abab'
has 'ababcabab'

run 'help prefers an exact match' '#help in' ''
has '#in [<label> [(<delay>) [<command>]]]'
lacks 'Normally, powwow prints on screen some messages each time you'

run 'help accepts an abbreviation' '#help speedw' ''
has 'Execute a speedwalk sequence even if the speedwalk option is disabled.'

# make a save-file like those saved before the readline-like keys, with
# its C-w bound to the function &$1
old_savefile() {
    run 'save a file to make an old one' "#save $tmp/cfg"
    script='s/^#savefile-version .*/#savefile-version 8/'
    for f in abort reverse-search forward-search kill-to-bol yank yank-pop \
             capitalize-word to-history insert-char insert-emoji; do
        script="$script
/=&$f\$/d"
    done
    sed -e "$script
/^#bind Delete /d
s/^#bind C-h /#bind BS /
s/^#bind M-C-h /#bind M-BS /
s/^#bind Backspace /#bind Del /
s/^#bind C-w .*/#bind C-w ^W=\\&$1/" "$tmp/cfg" > "$tmp/old"
}

old_savefile to-history
echo '#bind M-c ^[c=&upcase-word' >> "$tmp/old"
run 'new default keys are added to an old save-file' "#load $tmp/old
#bind edit"
has '#keys renamed: BS to C-h, M-BS to M-C-h, Del to Backspace'
has '#new default editing keys added: C-g, C-r, C-s, C-u, C-x-8-Ret, C-y, M-y, M-q,'
has '#    Delete, C-x-8-e-e, C-x-8-e-s'
has '#bind C-h ^H=&del-char-left'
has '#bind Delete ^[[3~=&del-char-right'
has '#C-w now deletes back to the previous space; M-q puts the line in history'
has '#bind C-w ^W=&del-unix-word-left'
has '#bind M-c ^[c=&upcase-word'
has '#bind C-r ^R=&reverse-search'

old_savefile clear-line
run 'an old save-file keeps its own C-w' "#load $tmp/old
#bind edit"
has '#bind C-w ^W=&clear-line'
lacks '#C-w now deletes back to the previous space; M-q puts the line in history'

run '#reset bind resets one key' '#bind M-c ^[c=&upcase-word
#reset bind M-c
#bind KP8 ^[Ox=north
#reset bind KP8
#bind foo ^[[20~=bar
#reset bind foo
#reset bind nosuch'
has '#default key binding: M-c ^[c=&capitalize-word'
has '#default key binding: KP8 ^[Ox=n'
has '#deleting key binding: foo ^[[20~=bar'
has '#no key named "nosuch", and no default for it.'

run '#reset rejects unknown lists' '#bind foo ^[[20~=bar
#reset bind 
#bind foo
#reset nosuch'
has '#no such key: "foo"'
has '#reset: unknown list "nosuch", must specify one of:'

run '#bind functions lists all editing functions' '#bind functions'
has '#moving:'
has '#killing and yanking:'
has '&abort                  stop searching (C-g)'
has '&clear-line             clear the input line'
has '&prev-line              show the previous history line (C-p, Up)'
has '&del-char-left          delete the character before the cursor (Backspace, C-h)'
has '&del-char-right         delete the character under the cursor (C-d, Delete)'
long=$(sed -n '/#editing functions:/,$p' "$tmp/out" | awk 'length > 79')
[ -z "$long" ] || fail "lines longer than 79 columns: $long"

run '#hilite sets the attributes of messages and input, and saves them' \
    "#hilite messages bold
#substitute
#hilite input red
#help -P beep
#save $tmp/cfg"
has '<ESC>[1m#no substitutions are defined.<ESC>[m'
has '<ESC>[1m#beep<ESC>[m'
grep -Fxq '#hilite input red' "$tmp/cfg" || fail "#hilite input not saved"
grep -Fxq '#hilite messages bold' "$tmp/cfg" || fail "#hilite messages not saved"

run 'help aligns the list of commands' '#help' ''
has '#addstatic {<text>|(<expr>)}    add the string to the static completion list'
long=$(sed -n '/#commands available:/,$p' "$tmp/out" | awk 'length > 79')
[ -z "$long" ] || fail "lines longer than 79 columns: $long"
has '#beep                           make your terminal beep (like #print (*7))'
has '#if (<expr>) <command1> [; #else <command2>]'
has '                                execute <command2>'

# a background pager must not write over the screen
POWWOWPAGER='&printf "one\ntwo\n"; echo err >&2; printf partial #'
run --until '#editor: partial' 'background editor output' '#help mark' ''
POWWOWPAGER=cat
has '#editor: one'
has '#editor: two'
has '#editor: err'
has '#editor: partial'

# a pager that would fail the test if it were used
POWWOWPAGER='echo PAGER USED #'
run 'help without the pager' '#help -P help
#help --no-pager
#help -P'
POWWOWPAGER=cat
lacks 'PAGER USED'
has '#help [-P|--no-pager] [keys|math|<command>]'
has '#addstatic {<text>|(<expr>)}    add the string to the static completion list'
lacks "Help on 'help'"

# run from the build directory, powwow finds the help in the source tree
unset POWWOWHELP
run 'help from the source tree' '#help -P keys'
POWWOWHELP=${srcdir:-.}/../doc
export POWWOWHELP
has 'C-x 8 Ret   &insert-char'

run 'Tab completes commands and their arguments' '' \
    "#he	speedw	
#reset b	C-x-8-e			$(printf '\025')
"
has 'Execute a speedwalk sequence even if the speedwalk option is disabled.'
has 'C-x-8-e-e  C-x-8-e-s'

run 'Tab lists the commands right after completing part of the name' '' \
    "#rebi		$(printf '\025')
"
grep -aq '#rebind  *#rebindALL  *#rebindall' "$tmp/out" || fail "expected the list"

run 'Tab completes the flags of #help and the topic after them' '' \
    "#help -P speedw	
#help --n	speedw	
"
[ "$(grep -c 'Execute a speedwalk sequence' "$tmp/out")" -eq 2 ] \
    || fail "expected the help on #speedwalk twice"

run 'Tab completes copyright, and the arguments of #quote and #ver' '' \
    "#help -P cop	
#quote of	
#ver -	
"
has 'Public License instead of this License.'
has '#normal mode.'
grep -q '^Options: ' "$tmp/out" || fail "expected the options of #ver -v"

run 'Tab completes the arguments of #delim, #option and #group' '' \
    "#del	pr	
#delim
#option +autop	
#alias k@fights=kill
#group fi	of	
"
grep -aq '^#delim: "program" ' "$tmp/out" || fail "expected #delim program"
has '#option autoprint is now on.'
grep -aq 'fights off $' "$tmp/out" || fail "expected #group fights off"

run 'Tab completes the names of aliases, actions, marks and variables' '' \
    "#alias kfoo=kill foo
#action >+hpcheck ^HP:&1=#print found
#mark troll=bold
#var \$target=orc
#alias kf	
#action -hpc	
#mark tr	
#var \$tar	
"
grep -aq '#alias kfoo $' "$tmp/out" || fail "expected #alias kfoo"
grep -aq '#action -hpcheck $' "$tmp/out" || fail "expected #action -hpcheck"
grep -aq '#mark troll $' "$tmp/out" || fail "expected #mark troll"
grep -aq '#var \$target $' "$tmp/out" || fail "expected #var \$target"

run 'the spaces after a file name are not part of it' "#save $tmp/saved "
[ -f "$tmp/saved" ] || fail "expected $tmp/saved"

mkdir -p "$tmp/defs/sub"
echo '#print ("load" + "ed")' > "$tmp/defs/mud.cfg"
run 'Tab completes file names, and directories without a space' '' \
    "#load $tmp/def	mu	
#capture >$tmp/defs/s	
#save $tmp/defs/		$(printf '\025')
"
has 'loaded'
has "#error writing file \"$tmp/defs/sub/\""
has 'mud.cfg  sub/'

run 'Tab completes the arguments of #hilite' '' \
    "#hil	inp	un	Ye	o	cy	
#hil	me	b		
"
grep -aq 'input highlighting is now <ESC>\[4;93;46mon' "$tmp/out" \
    || fail "expected underline Yellow on cyan"
has 'black  blink  blue   bold'

run 'Tab lists the words down and then across' '' \
    "#option 		$(printf '\025')
"
has 'autoclear  debug      exit       keyecho    reprint    speedwalk'
has 'compact    editor=    info       pager=     shell=     wrap'

# the texts that #help shows must be ASCII, except for what tty_put_wc()
# writes as ASCII where the locale is not UTF-8: the copyright sign, and
# the dashes U+2010 to U+2015
name='the texts of #help have no other non-ASCII characters'
copyright=$(printf '\302\251')
dash=$(printf '\342\200[\220-\225]')
high=$(printf '[\200-\377]')
: > "$tmp/out"
for f in "${srcdir:-.}/../COPYING" "${srcdir:-.}/../doc/unicode-license.txt" \
         "${srcdir:-.}/../doc/powwow.help"; do
    LC_ALL=C sed -e "s/$copyright//g" -e "s/$dash//g" "$f" \
        | LC_ALL=C grep -n "$high" | sed "s|^|${f##*/}:|" >> "$tmp/out"
done
[ -s "$tmp/out" ] && fail "replace these, or write them as ASCII in tty_put_wc()"

# powwow-movieplay -a converts a movie to text, without its pauses
name='powwow-movieplay -a'
printf 'line hello\nsleep 60000\nprompt HP:10> \n' > "$tmp/movie"
: > "$tmp/out"
./powwow-movieplay -a "$tmp/movie" "$tmp/text" || fail "exit status $?"
[ "$(cat "$tmp/text")" = "hello
HP:10> " ] || fail "unexpected text: $(cat "$tmp/text")"
[ "$(./powwow-movieplay -a < "$tmp/movie")" = "$(cat "$tmp/text")" ] \
    || fail "unexpected text on standard output"

run '#save and the end of #capture, #movie and #record name the file' \
    "#save $tmp/defs.cfg
#capture >$tmp/cap
#capture
#movie $tmp/mov
#movie
#record $tmp/rec
#record"
has "#settings saved to \"$tmp/defs.cfg\"."
has "#end of capture to \"$tmp/cap\"."
has "#end of movie to \"$tmp/mov\"."
has "#end of record to \"$tmp/rec\"."

long=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
run 'messages wrap before the words that do not fit' "#alias $long"
has '#unknown alias, cannot show:'
has "#    \"$long\""

run 'help on a command without an entry' '#help nosuchcommand'
has '#no entry for "nosuchcommand" in the help file.'

run 'error message' '#connect test --no-tls 127.0.0.1 1'
has '#cannot connect to 127.0.0.1:1: Connection refused'

run 'random numbers' "$(for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    echo '#print (rand 6)'
done)"
grep -Ex '[0-9]+' "$tmp/out" > "$tmp/numbers"
[ "$(wc -l < "$tmp/numbers")" -eq 20 ] || fail "expected 20 numbers"
grep -Evx '[0-5]' "$tmp/numbers" && fail "number out of range"
[ "$(sort -u "$tmp/numbers" | wc -l)" -gt 1 ] || fail "numbers are all the same"

run 'version' '#ver'
joined \
    | grep -q '^Powwow version .*, Copyright 1998-[0-9]* by the powwow authors$' \
    || fail "missing version"
grep -q '^Options: ' "$tmp/out" && fail "unexpected options"
grep -q '^Authors: ' "$tmp/out" && fail "unexpected authors"

run 'verbose version' '#ver -v'
grep -q '^Options: ' "$tmp/out" || fail "missing options"
has 'Authors: Massimiliano Ghilardi (Cosmos), Steve Slaven (bpk), Fror, Dain and others'
has 'https://github.com/MUME/powwow'
lacks '#syntax: #ver [-v]'

run 'jit substitution without a closing brace' '#var $v=V
#alias t1=#print [${v x}] [${v}]
#alias t2=#print [@{9 x}] [${v
t1
t2'
has '[${v x}] [V]'
has '[@{9 x}] [${v'

run 'jit substitution after an undefined variable' '#var $v=V
#alias t=#print [${undef}${v}] [${undef}@{9}]
t'
has '[V] [0]'

run 'matching an action keeps the escapes of its pattern' \
    '#action >a x\\\$y=#print ("m")
#emulate x$y
#emulate x$y
#emulate x$y
#action'
has '#action >+a@* x\\\$y=#print ("m")'
lacks '#missing digit after "$y"'

run 'action pattern longer than a buffer' '#($v = "x" * 5000)
#action >a ${v}=#print ("matched")
#emulate y
#print ("alive")'
has 'y'
lacks 'matched'
has 'alive'

printf '#alias t=#print loaded\n' > "$tmp/def"
run --args "$tmp/def 127.0.0.1 1" 'definition file, host and port' 't'
has '#cannot connect to 127.0.0.1:1: Connection refused'
has 'loaded'

# a line longer than the memory limit is still processed as a line, not
# again as a prompt
awk 'BEGIN { s = sprintf("%150s", ""); gsub(/ /, "x", s); print s
             print "after" }' > "$tmp/long"
run 'line longer than the memory limit' "#setvar mem=120
#emulate <$tmp/long"
has '#error: text/string longer than limit, discarded.'
has 'after'
lacks '#out of memory'

# a spawned command is a connection that uses the telnet protocol too:
# what the command prints is received, and what is sent is its input
cat > "$tmp/server" <<'EOF'
printf '\377\375\037'
od -An -tu1 -N12 | tr -s ' ' | sed -e 's/^/#print got/' -e 's/ *$//'
EOF
run --until '#connection on "n" closed.' \
    'window size for a connection that is not the main one' \
    "#spawn n sh $tmp/server"
joined | grep -Eqx 'got 255 251 31 255 250 31( [0-9]+){4} 255 240' \
    || fail "missing the window size"

# a suboption that fills the buffer, overflowing it without the fix when
# built with -fsanitize=address
cat > "$tmp/server" <<'EOF'
printf '\377\372\052\001;ISO-8859-1'
i=0
while [ $i -lt 150 ]; do
    printf ';x'
    i=$((i + 1))
done
printf '\377\360'
EOF
run --until '#connection on "n" closed.' 'suboption longer than the buffer' \
    "#spawn n sh $tmp/server"
has '#charset: iso-8859-1 (negotiated)'

# the command cancels the session as soon as it starts, without a main
# connection
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n#cancel\n'
cat > /dev/null
EOF
run --until '#cancelled session' \
    'cancelling an editing session without a main connection' \
    "#option editor=&sh -c 'sleep 5'
#spawn n sh $tmp/server
##n #request editor"
has '#killed "descr" (5)'

# a suboption from one connection, split by a suboption from another one
cat > "$tmp/server" <<'EOF'
printf '\377\372\052\001;ISO-8859-1'
touch "$1/sent"
until [ -e "$1/done" ]; do
    sleep 1
done
printf '\377\360'
EOF
cat > "$tmp/other" <<'EOF'
until [ -e "$1/sent" ]; do
    sleep 1
done
printf '\377\372\143\377\360'
sleep 1
touch "$1/done"
EOF
run --until '#connection on "n" closed.' \
    'suboptions from two connections at the same time' \
    "#spawn n sh $tmp/server $tmp
#spawn o sh $tmp/other $tmp"
has '#charset: iso-8859-1 (negotiated)'

# MPI messages with a length of 0 or one too large for an int
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE\n#print one\n'
printf '~$#EE4294967295\n#print two\n'
EOF
run --until '#connection on "n" closed.' 'MPI messages with a bad length' \
    "#spawn n sh $tmp/server
##n #request editor"
[ "$(grep -c '^#warning: bad MPI message header$' "$tmp/out")" -eq 2 ] \
    || fail "expected two bad headers"
lacks '#out of memory'
has 'two'

# every command must have a help entry with the syntax shown by #help, and
# the manual must have it too, except for the commands it does not show
name='every command has the same syntax in the help'
no_manual=' beep clear connect cpu group host net qui request snoop time zap '
tr '\n' ' ' < "${srcdir:-.}/cmd.c" | awk '{
    n = split($0, parts, /C\("/)
    for (i = 2; i <= n; i++) {
        p = parts[i]
        command = "@" substr(p, 1, index(p, "\"") - 1)
        p = substr(p, index(p, "\"") + 1)
        p = substr(p, index(p, "\"") + 1)
        print command "\t" substr(p, 1, index(p, "\\t") - 1)
    }
}' > "$tmp/commands"
while IFS='	' read -r command args; do
    command=${command#@}                    # "@" keeps an empty name
    [ "$command" != 17 ] || continue        # an example of #<number>
    syntax="#$command${args:+ $args}"
    case $command in
        [[:punct:]]) syntax="#$command$args" ;;   # no space, as #!ls
    esac
    if ! grep -Fxq -e "@${command:-()}" "$POWWOWHELP/powwow.help"; then
        echo "FAIL $name: no help entry for #$command"
        failed=1
    elif ! grep -Fxq -e "$syntax" "$POWWOWHELP/powwow.help"; then
        echo "FAIL $name: not in powwow.help: $syntax"
        failed=1
    fi
    case $command in
        '') ;;                              # #(<expr>)
        *)
            case $no_manual in
                *" $command "*) continue ;;
            esac
            if ! grep -Fxq -e "$syntax" "$POWWOWHELP/powwow.md.in"; then
                echo "FAIL $name: not in powwow.md.in: $syntax"
                failed=1
            fi ;;
    esac
done < "$tmp/commands"

exit $failed
