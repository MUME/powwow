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
    sed -e "s/$esc\[K//g" -e "s/$cr//g" -e "s/$esc/<ESC>/g" "$tmp/raw" > "$tmp/out"
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
has '<ESC>[1mbar<ESC>[0m'

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
has '<ESC>[1mfoo<ESC>[0m x'

run 'mark the first word' '#mark ^$=bold
#print ("foo x bar")'
has '<ESC>[1mfoo<ESC>[0m x bar'

run 'mark with 256 and RGB colors' '#mark foo=bold 208 on #202020
#mark bar=italic strike on 22
#mark
#print ("foo bar")'
has '#new mark: foo=bold 208 on #202020'
has '#mark foo=bold 208 on #202020'
has '#mark bar=italic strike on 22'
has '<ESC>[1;38;5;208;48;2;32;32;32mfoo<ESC>[0m <ESC>[3;9;48;5;22mbar<ESC>[0m'

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

run 'NUL is not a word delimiter' '#($a = "a  \000  b")
#print (:?$a)'
has '3'

run 'transpose words at the end of a line' '#bind foo=&transpose-words' \
    "$esc[A" "#print foo bar $esc[A
"
has 'bar  foo'

run 'upcase a word at the end of a line' '#bind foo=&upcase-word' \
    "$esc[A" "#print foo bar $esc[A
"
has 'foo BAR '

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

run 'help on a command without an entry' '#help nosuchcommand'
has '#no entry for "nosuchcommand" in the help file.'

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
