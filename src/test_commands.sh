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

# start test $1, running the commands in $2; then, if given, type the keys in
# $3 once powwow is waiting for them, and then the keys in $4
run() {
    name=$1
    {
        printf '%s\n' "$2"
        if [ $# -gt 2 ]; then
            sleep 1
            printf '%s' "$3"
            sleep 1
            if [ $# -gt 3 ]; then
                printf '%s' "$4"
                sleep 1
            fi
        fi
        printf '#quit\n'
    } | "$POWWOW" > "$tmp/raw" 2>&1 &
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
    grep -Fxq -e "$1" "$tmp/out" || fail "missing line: $1"
}

# the output must not have a line that is exactly $1
lacks() {
    grep -Fxq -e "$1" "$tmp/out" && fail "unexpected line: $1"
}

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
has '#invalid attribute syntax.'
has '#no markers are defined.'

run 'attr operator' '#print (attr "dim #ff8700" + "x")'
has '<ESC>[2;38;2;255;135;0mx'

run 'substitute the first word' '#substitute ^$=Z
#print ("foo x bar")'
has 'Z x bar'

run 'emulate without a connection' '#emulate hello'
has 'hello'

run 'bind reads a whole key sequence' '#bind foo=bar' "$esc[A"
has '#new key binding: foo ^[[A=bar'

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

run 'transpose words at the end of a line' '#bind foo=&transpose-words' \
    "$esc[A" "#print foo bar $esc[A
"
has 'bar  foo'

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

run 'C-n after a search goes on to the line being typed' "$history" \
    "#print typed${cr}ban${ce}$(printf '\016\016')
"
has 'typed'
lacks 'bananatyped'

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
    LC_ALL=C
fi

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

run 'rebind rejects an empty key sequence' '#bind foo=bar' "$esc[A" "#rebind foo =
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

run 'help prefers an exact match' '#help in' ''
has '#in [label [(delay in millisec.) [command]]]'
lacks 'Normally, powwow prints on screen some messages each time you'

run 'help accepts an abbreviation' '#help speedw' ''
has 'Execute a speedwalk sequence even if the speedwalk option is disabled.'

run '#bind functions lists all editing functions' '#bind functions'
has '&abort                  stop searching (C-g)'
has '&clear-line             clear the input line'
has '&prev-line              show the previous history line (C-p, Up)'
long=$(sed -n '/#editing functions:/,$p' "$tmp/out" | awk 'length > 79')
[ -z "$long" ] || fail "lines longer than 79 columns: $long"

run 'help aligns the list of commands' '#help' ''
has '#addstatic {string|(expr)}      add the string to the static completion list'
long=$(sed -n '/#commands available:/,$p' "$tmp/out" | awk 'length > 79')
[ -z "$long" ] || fail "lines longer than 79 columns: $long"
has '#beep                           make your terminal beep (like #print (*7))'
has '#if (expr) instr1 [; #else instr2]'
has '                                otherwise execute "instr2"'

# a background pager must not write over the screen
POWWOWPAGER='&printf "one\ntwo\n"; echo err >&2; printf partial #'
run 'background editor output' '#help mark' ''
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
has '#help [-P | --no-pager] [keys | math | command]'
has '#addstatic {string|(expr)}      add the string to the static completion list'
lacks "Help on 'help'"

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
grep -q '^Powwow version ' "$tmp/out" || fail "missing version"
grep -q '^Options: ' "$tmp/out" && fail "unexpected options"

run 'verbose version' '#ver -v'
grep -q '^Options: ' "$tmp/out" || fail "missing options"
lacks '#syntax: #ver [-v]'

# every named command must have a help entry
name='every command has a help entry'
sed -n 's/^ *C("\([^"][^"]*\)".*/\1/p' "${srcdir:-.}/cmd.c" > "$tmp/commands"
while read -r command; do
    if ! grep -Fxq -e "@$command" "$POWWOWHELP/powwow.help"; then
        echo "FAIL $name: $command"
        failed=1
    fi
done < "$tmp/commands"

exit $failed
