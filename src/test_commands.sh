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
# the temporary files, and the lock files of definition files, in $tmp too
TMPDIR=$tmp
export TMPDIR
unset XDG_RUNTIME_DIR
# the colors of xterm, which has 8, not those of the terminal of the tests
unset COLORTERM WT_SESSION
# the user-wide settings file is then in $HOME, where the tests put it
unset APPDATA XDG_CONFIG_HOME
# that file, which tests that need another write over, without the
# highlighting of powwow, so that what powwow prints has no attributes
no_hilite() {
    mkdir -p "$tmp/.config/powwow"
    printf '#hilite input none\n#hilite messages none\n' \
        > "$tmp/.config/powwow/powwow.conf"
}
no_hilite
# fill memory when it is allocated and freed, on macOS and with glibc, so
# that using a field that was never set, or freed memory, finds no zeros
MallocPreScribble=1
MallocScribble=1
MALLOC_PERTURB_=170
export MallocPreScribble MallocScribble MALLOC_PERTURB_
unset POWWOWDIR POWWOWHELP
# the commands of the user, which would show in the tests
unset EDITOR PAGER POWWOWEDITOR POWWOWHELPPAGER POWWOWSHELL
POWWOWHELP=${srcdir:-.}/../doc
POWWOWPAGER=cat
export POWWOWHELP POWWOWPAGER

esc=$(printf '\033')
cr=$(printf '\r')
failed=0

# report the failure of test $name, for the reason $1, with the start of
# what powwow printed
fail() {
    echo "FAIL $name: $1"
    cut -c 1-500 "$tmp/out" | sed -e 's/^/    /' -e 200q
    [ "$(wc -l < "$tmp/out")" -le 200 ] || echo '    [...]'
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

# type the keys in $1 one at a time, each once powwow has drawn the one
# before, so that it reads them one at a time
type_one_by_one() {
    keys=$1
    while [ -n "$keys" ]; do
        rest=${keys#?}
        size=$(wc -c < "$tmp/raw")
        printf '%s' "${keys%"$rest"}"
        i=0
        until [ "$(wc -c < "$tmp/raw")" -gt "$size" ] || [ $i -ge 500 ]; do
            sleep 0.01
            i=$((i + 1))
        done
        keys=$rest
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
# --until TEXT to wait for powwow to print TEXT before quitting,
# --args ARGS to give powwow the arguments ARGS, split at spaces,
# --eof to end the input without a #quit, and --one-by-one to type the
# keys in $3 one at a time
run() {
    bind= until= args= quit=1 one_by_one=
    while :; do
        case $1 in
            --args) args=$2; shift 2 ;;
            --bind) bind=1; shift ;;
            --eof) quit=; shift ;;
            --one-by-one) one_by_one=1; shift ;;
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
                if [ -n "$one_by_one" ]; then
                    type_one_by_one "$3"
                else
                    printf '%s' "$3"
                fi
            fi
            sync_powwow 2
            if [ $# -gt 3 ]; then
                printf '%s' "$4"
                sync_powwow 3
            fi
        fi
        [ -z "$until" ] || wait_for "$until"
        [ -z "$quit" ] || printf '#quit\n'
    } | (
        # stop powwow if it prints a flood of text, such as when it hangs
        ulimit -f 4096 2>/dev/null
        exec "$POWWOW" $args
    ) > "$tmp/raw" 2>&1 &
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

run --eof 'powwow quits at the end of its input' '#print ("at" + " end")'
has 'at end'

run --eof 'the messages at the end of the input are not in its attribute' \
    '#hilite input bold'
grep -aq '<ESC>\[1m<ESC>[^#]*#warning: no save-file defined' "$tmp/out" \
    || fail 'expected the end of the bold before the warning'

# reading a directory fails on most systems
if ! cat < / > /dev/null 2>&1; then
    name='powwow quits when reading its input fails'
    "$POWWOW" < / > "$tmp/out" 2>&1 &
    pid=$!
    (sleep 5; kill -9 $pid) > /dev/null 2>&1 &
    watchdog=$!
    wait $pid
    kill $watchdog 2>/dev/null
    wait $watchdog 2>/dev/null
    grep -q '^#fatal error: cannot read from the terminal' "$tmp/out" \
        || fail 'no error for reading the input'
fi

run --eof '#bind gives up waiting for a key at the end of the input' \
    '#bind foo=bar'
has '#please press the key "foo" : #unable to get key. Giving up.'

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

run '#bind lists the keys that run commands, also the default ones' '#bind'
has '#keys that run commands:'
has '#bind KP7 ^[Ow=look'

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

run 'without UTF-8, the greeting and the help quote commands with '"'" \
    '#help history
#help -P history'
grep -Fq "for details type '#help warranty'." "$tmp/out" \
    || fail "expected '#help warranty' quoted"
[ "$(grep -c "^'#history' alone shows" "$tmp/out")" -eq 2 ] \
    || fail "expected '#history' quoted, with and without the pager"

run 'help copyright shows all of COPYING, with and without the pager' \
    '#help copyright
#help -P copyright'
[ "$(grep -c 'is copyrighted by the$' "$tmp/out")" -eq 2 ] \
    || fail "missing the first line of COPYING"
[ "$(grep -c '^Public License instead of this License\.$' "$tmp/out")" \
      -eq 2 ] || fail "missing the last line of COPYING"
[ "$(grep -c '^UNICODE LICENSE V3$' "$tmp/out")" -eq 2 ] \
    || fail "missing the license of the Unicode data"
# the line in COPYING, and that between the licenses
[ "$(grep -c '^-\{79\}$' "$tmp/out")" -eq 4 ] \
    || fail "expected lines of dashes before the licenses"

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

run '#load does not report the #option editor it sets' "#load $tmp/options.pw
#option"
lacks '#option editor is now: my ed\;it -x'
has '#option editor=my ed\;it -x'

# a file name in quotes is not broken at its spaces when wrapping, but goes
# on a line of its own
dir="$tmp/Application Support/powwow with a long name"
run 'wrapping does not break a file name in quotes' "#file =$dir/x.powwow"
has "#    '$dir/x.powwow'"

run '#setvar buffer rejects sizes too large for powwow' \
    '#setvar buffer=4294968296
#setvar'
has '#buffer size must be 0 (zero) or from 1000 to 2147483647'
has '#setvar buffer=0'

run '#setvar lines rejects numbers out of range' \
    '#setvar lines=30
#setvar lines=4294967297
#setvar lines=0
#setvar'
has '#lines must be from 1 to 2147483647'
has '#setvar lines=30'

run '#setvar reads numbers as expressions do, and rejects others' \
    '#setvar lines=030
#setvar mem=0x10
#setvar mem=12x
#setvar buffer=#7d0
#setvar'
has '#setvar: not an integer: 0x10'
has '#setvar: not an integer: 12x'
has '#setvar buffer=2000'
has '#setvar lines=30'
has '#setvar mem=1048576'

run 'texts are limited to 1 MB by default' '#setvar
#($a = "x" * 1048576)
#($a = "x" * 1048577)'
has '#setvar mem=1048576'
[ "$(grep -c '^#error: text/string longer than limit, discarded\.$' \
       "$tmp/out")" -eq 1 ] || fail 'expected one error for the limit'

run '#save saves #setvar mem=0' "#file =$tmp/mem.pw
#setvar mem=0
#save"
run --args "$tmp/mem.pw" 'loading #setvar mem=0' '#setvar'
has '#setvar mem=0'

run '#setvar editmem is 16777216 by default, which #save leaves out' \
    "#file =$tmp/editmem-default.pw
#setvar
#save"
has '#setvar editmem=16777216'
grep -q editmem "$tmp/editmem-default.pw" \
    && fail 'expected no #setvar editmem in the file'

run '#save saves a changed #setvar editmem' "#file =$tmp/editmem.pw
#setvar editmem=0
#save"
run --args "$tmp/editmem.pw" 'loading #setvar editmem=0' '#setvar'
has '#setvar editmem=0'

run --args "$tmp/editmem.pw" \
    '#load of a file without #setvar editmem sets the default' \
    "#load $tmp/editmem-default.pw
#setvar"
has '#setvar editmem=16777216'

printf '#alias kept=yes\n' > "$tmp/save.pw"
run --args "$tmp/save.pw" '#option -save keeps the definition file on exit' \
    '#option -save
#alias added=yes'
grep -q 'added' "$tmp/save.pw" && fail 'expected the file as it was'
run --args "$tmp/save.pw" '#option +save saves the definition file on exit' \
    '#alias added=yes'
grep -Fxq '#alias added=yes' "$tmp/save.pw" || fail 'expected the alias saved'
grep -Fq '+save' "$tmp/save.pw" || fail 'expected #option +save saved'

# the lines of #history must have the line $1, or none that contain $1
has_history() {
    grep -Fxq -e "$1" "$tmp/history" || fail "not in the history: $1"
}
lacks_history() {
    grep -Fq -e "$1" "$tmp/history" && fail "in the history: $1"
}
# the lines of #history in the output, without their numbers
history() {
    sed -n 's/^# *[0-9]*: //p' "$tmp/out" > "$tmp/history"
}

run '#secret keeps lines with a secret out of the history' "#secret add
hunter22
#put say hunter22 now
#put keep this
#add hunter22x hunter21x
say password=hunter22!
#history
#file =$tmp/secret.pw
#option +history
#option +words
#save"
history
has '#secret number 1 added.'
lacks 'hunter22'
lacks_history hunter22
has_history '#secret add'
has_history 'keep this'
has_history '#put keep this'
grep -q '^#secret =[0-9a-f]\{16\}:8:[0-9a-f]\{8\}$' "$tmp/secret.pw" \
    || fail 'expected the hash of the secret in the file'
grep -q 'hunter22' "$tmp/secret.pw" && fail 'the secret is in the file'
grep -Fxq '#put keep this' "$tmp/secret.pw" \
    || fail 'expected the other lines in the file'
grep -q 'hunter21x' "$tmp/secret.pw" \
    || fail 'expected the other words in the file'

run --args "$tmp/secret.pw" 'loading #secret' '#secret
#put say hunter22
#history'
history
has '#1: 8 bytes'
lacks_history hunter22
has_history 'keep this'

# the user-wide settings file, read before the definition file; without
# --config, #save puts its settings in the definition file
printf '#option editor=from-config\n' >> "$tmp/.config/powwow/powwow.conf"
printf '#alias kept=yes\n' > "$tmp/shared.pw"
run --args "$tmp/shared.pw" 'powwow reads the user-wide settings file' \
    '#option'
has '#option editor=from-config'
grep -Fxq '#option editor=from-config' "$tmp/shared.pw" \
    || fail 'expected the user-wide settings in the definition file'
run --args --no-config '--no-config skips the user-wide settings file' \
    '#option'
lacks '#option editor=from-config'
no_hilite

if [ "$(uname -s)" = Darwin ]; then
    conf="$tmp/Library/Application Support/powwow/powwow.conf"
    mkdir -p "${conf%/*}"
    mv "$tmp/.config/powwow/powwow.conf" "$conf"
    printf '#option editor=from-library\n' >> "$conf"
    rm -r "$tmp/.config"
    run 'on macOS, the user-wide settings file is in ~/Library' '#option'
    has '#option editor=from-library'
    rm -r "$tmp/Library"
    no_hilite
fi

# without a #hilite, typed text is bold, and the messages italic Blue
run --args --no-config 'powwow highlights in bold and italic Blue' '#hilite'
has '<ESC>[3;94m#hilite input bold (user-wide default)<ESC>[m'
has '<ESC>[3;94m#hilite messages italic Blue (user-wide default)<ESC>[m'

# with --config, the user-wide settings go in their own file, which powwow
# creates with its folders, and the others in the definition file
hash=$(sed -n 's/^#secret =//p' "$tmp/secret.pw" | head -n 1)
printf '#alias kept=yes\n#option editor=local\n' > "$tmp/launched.pw"
run --args "--config=$tmp/new/dir/powwow.conf $tmp/launched.pw" \
    '--config saves the user-wide settings in their own file' \
    "#option editor=global
#secret =$hash
#option -save
#save"
grep -Fxq '#option editor=global' "$tmp/new/dir/powwow.conf" \
    || fail 'expected #option editor in the user-wide settings file'
grep -Fxq "#secret =$hash" "$tmp/new/dir/powwow.conf" \
    || fail 'expected #secret in the user-wide settings file'
grep -Fxq '#option -save' "$tmp/new/dir/powwow.conf" \
    || fail 'expected #option -save in the user-wide settings file'
grep -q 'editor=\|^#secret\|[+-]save' "$tmp/launched.pw" \
    && fail 'expected no user-wide settings in the definition file'
grep -Fxq '#alias kept=yes' "$tmp/launched.pw" \
    || fail 'expected the alias in the definition file'
printf '#hilite input none\n#hilite messages none\n' \
    >> "$tmp/new/dir/powwow.conf"
run --args "--config=$tmp/new/dir/powwow.conf" \
    '--config reads the user-wide settings file it names' '#option'
has '#option editor=global'

# the #hilite of the user-wide settings file is user-wide, and a MUD's own
# wins over it until "default" drops it; only a MUD's own is saved in its
# definition file
printf '#hilite input underline\n#hilite messages underline\n' > "$tmp/hl.conf"
printf '#option +info\n#hilite input red\n' > "$tmp/hl.pw"
run --args "--config=$tmp/hl.conf $tmp/hl.pw" \
    "a MUD's own #hilite wins over the user-wide one" \
    '#print ("plain")
#hilite input default
#hilite messages none
#save'
grep -aq 'input highlighting is now .*<ESC>\[4mon<ESC>\[m.* (user-wide def' \
    "$tmp/out" || fail 'expected the user-wide underline after default'
has '#message highlighting is now off.'
grep -Fxq '#hilite messages none' "$tmp/hl.pw" \
    || fail "expected the MUD's own #hilite messages in its file"
grep -Fq '#hilite input' "$tmp/hl.pw" \
    && fail 'expected no #hilite input in the definition file'
grep -Fxq '#hilite input underline' "$tmp/hl.conf" \
    || fail 'expected the user-wide #hilite input to stay'
grep -Fxq '#hilite messages underline' "$tmp/hl.conf" \
    || fail 'expected the user-wide #hilite messages to stay'
# without an attribute, #hilite shows it, or both, and off turns it off
run --args "--config=$tmp/hl.conf $tmp/hl.pw" \
    '#hilite without an attribute shows it, and off turns it off' \
    '#hilite
#hilite input off
#hilite input'
has '#hilite input underline (user-wide default)'
has '#hilite messages none'
has '#hilite input none'
lacks '#hilite input none (user-wide default)'
# without input or messages, off and default are for both
run --args "--config=$tmp/hl.conf $tmp/hl.pw" \
    '#hilite off and default are for both' \
    '#hilite off
#hilite
#hilite default
#hilite'
has '#input highlighting is now off.'
has '#message highlighting is now off.'
has '#hilite input none'
has '#hilite messages none'
# the messages are underlined again
for k in input messages; do
    grep -aq "#hilite $k underline (user-wide default)" "$tmp/out" \
        || fail "expected the user-wide #hilite $k after default"
done
# without --config, the user-wide one goes in the definition file too
mkdir -p "$tmp/.config/powwow"
printf '#hilite input underline\n' > "$tmp/.config/powwow/powwow.conf"
printf '#alias kept=yes\n' > "$tmp/hl2.pw"
run --args "$tmp/hl2.pw" \
    'without --config, #save saves the user-wide #hilite too' '#save'
grep -Fxq '#hilite input underline' "$tmp/hl2.pw" \
    || fail 'expected the user-wide #hilite input in the definition file'
no_hilite

# --get prints one setting, and --set saves one, without a session, as
# the launcher uses them; only the value goes to standard output
name='--set saves a setting and --get prints it'
conf="$tmp/batch/powwow.conf"
# as --get prints paths: on Cygwin, as on Windows, for the launcher
case $(uname -s) in
    CYGWIN*) conf_shown=$(cygpath -w "$conf") ;;
    *) conf_shown=$conf ;;
esac
"$POWWOW" "--config=$conf" --set 'editor=vi -x' > "$tmp/out" 2>&1 \
    || fail '--set failed'
[ -s "$tmp/out" ] && fail 'expected no output from --set'
[ "$("$POWWOW" "--config=$conf" --get editor 2> /dev/null)" = 'vi -x' ] \
    || fail 'expected --get to print the editor'
[ "$("$POWWOW" "--config=$conf" --get save 2> /dev/null)" = on ] \
    || fail 'expected --get to print on for #option save'
name='--set and --get the user-wide #hilite'
"$POWWOW" "--config=$conf" --set 'hilite-input=bold Yellow on #202020' \
    > /dev/null 2>&1 || fail '--set hilite-input failed'
grep -Fxq '#hilite input bold Yellow on #202020' "$conf" \
    || fail 'expected #hilite input in the user-wide settings file'
[ "$("$POWWOW" "--config=$conf" --get hilite-input 2> /dev/null)" \
    = 'bold Yellow on #202020' ] || fail 'expected --get hilite-input'
"$POWWOW" "--config=$conf" --set hilite-input=purple > /dev/null 2>&1 \
    && fail 'expected --set hilite-input=purple to fail'
"$POWWOW" "--config=$conf" --set hilite-input= > /dev/null 2>&1 \
    || fail '--set hilite-input= failed'
grep -Fxq '#hilite input none' "$conf" \
    || fail 'expected #hilite input none once cleared'
[ -z "$("$POWWOW" "--config=$conf" --get hilite-input 2> /dev/null)" ] \
    || fail 'expected --get hilite-input to print nothing once cleared'
"$POWWOW" "--config=$conf" --set hilite-input=default > /dev/null 2>&1 \
    || fail '--set hilite-input=default failed'
grep -Fq '#hilite' "$conf" && fail "expected no #hilite for powwow's own"
[ "$("$POWWOW" "--config=$conf" --get hilite-input 2> /dev/null)" = bold ] \
    || fail "expected --get hilite-input to print powwow's own"
name="--set and --get the MUD's own #hilite"
printf '#hilite input red\n' > "$tmp/mh.pw"
[ "$("$POWWOW" "--config=$conf" --get mud-hilite-input "$tmp/mh.pw" \
    2> /dev/null)" = red ] || fail 'expected --get mud-hilite-input'
[ "$("$POWWOW" "--config=$conf" --get mud-hilite-messages "$tmp/mh.pw" \
    2> /dev/null)" = default ] || fail 'expected default for no #hilite'
"$POWWOW" "--config=$conf" --set mud-hilite-messages=off "$tmp/mh.pw" \
    > /dev/null 2>&1 || fail '--set mud-hilite-messages=off failed'
grep -Fxq '#hilite messages none' "$tmp/mh.pw" \
    || fail "expected the MUD's own #hilite messages in its file"
"$POWWOW" "--config=$conf" --set mud-hilite-input=default "$tmp/mh.pw" \
    > /dev/null 2>&1 || fail '--set mud-hilite-input=default failed'
grep -Fq '#hilite input' "$tmp/mh.pw" \
    && fail 'expected no #hilite input once default'
grep -Fq '#hilite' "$conf" && fail 'expected no #hilite in the user-wide file'
"$POWWOW" "--config=$conf" --set mud-hilite-input=bold > /dev/null 2>&1 \
    && fail 'expected --set mud-hilite-input to need a definition file'

name="#setvar keepalive is user-wide, and a MUD's own wins over it"
"$POWWOW" "--config=$conf" --set keepalive=300 > /dev/null 2>&1 \
    || fail '--set keepalive=300 failed'
grep -Fxq '#setvar keepalive=300' "$conf" \
    || fail 'expected the keepalive in the user-wide settings file'
printf '#alias k=x\n' > "$tmp/ka.pw"
[ "$("$POWWOW" "--config=$conf" --get mud-keepalive "$tmp/ka.pw" \
    2> /dev/null)" = default ] || fail 'expected default for no own keepalive'
"$POWWOW" "--config=$conf" --set mud-keepalive=0 "$tmp/ka.pw" \
    > /dev/null 2>&1 || fail '--set mud-keepalive=0 failed'
grep -Fxq '#setvar keepalive=0' "$tmp/ka.pw" \
    || fail "expected the MUD's own keepalive, off, in its file"
[ "$("$POWWOW" "--config=$conf" --get keepalive "$tmp/ka.pw" \
    2> /dev/null)" = 300 ] || fail 'expected the user-wide keepalive kept'
run --args "--config=$conf $tmp/ka.pw" \
    "a MUD's own keepalive wins over the user-wide one" '#setvar'
grep -Fq '#setvar keepalive=0<' "$tmp/out" \
    || fail "expected the MUD's own keepalive, 0"
"$POWWOW" "--config=$conf" --set mud-keepalive=default "$tmp/ka.pw" \
    > /dev/null 2>&1 || fail '--set mud-keepalive=default failed'
grep -Fq '#setvar keepalive' "$tmp/ka.pw" \
    && fail 'expected no keepalive in the definition file once default'
run --args "--config=$conf $tmp/ka.pw" \
    'the user-wide keepalive is used without a MUD of its own' '#setvar'
grep -Fq '#setvar keepalive=300 (user-wide default)' "$tmp/out" \
    || fail 'expected the user-wide keepalive, 300'
run '#setvar default is only for keepalive' '#setvar mem=default'
grep -Fq '#setvar: not an integer: default' "$tmp/out" \
    || fail 'expected default to be rejected for mem'
"$POWWOW" "--config=$conf" --set keepalive=86401 > /dev/null 2>&1 \
    && fail 'expected --set keepalive=86401 to fail'
"$POWWOW" "--config=$conf" --set keepalive=0 > /dev/null 2>&1 \
    || fail '--set keepalive=0 failed'
grep -Fq '#setvar keepalive' "$conf" \
    && fail 'expected no keepalive in the user-wide file once off'

name='--set saves as with --config, and needs a file for the others'
"$POWWOW" "--config=$conf" --set 'editor=vi -y' "$tmp/m.pw" \
    > /dev/null 2>&1 || fail '--set with a definition file failed'
grep -Fxq '#option editor=vi -y' "$conf" \
    || fail 'expected the editor in the user-wide settings file'
grep -Fq '#option editor=' "$tmp/m.pw" \
    && fail 'expected no editor in the definition file'
"$POWWOW" "--config=$conf" --set wrap=on > "$tmp/out" 2>&1 \
    && fail 'expected --set wrap without a definition file to fail'
grep -Fq '"wrap" needs a definition file' "$tmp/out" \
    || fail 'expected --set wrap to need a definition file'
"$POWWOW" "--config=$conf" --set 'editor=vi -x' > /dev/null 2>&1 \
    || fail '--set without a definition file failed'
name='--list-settings prints all the settings'
"$POWWOW" "--config=$conf" --list-settings > "$tmp/out" 2> /dev/null \
    || fail '--list-settings failed'
grep -Fxq 'editor=vi -x' "$tmp/out" \
    || fail 'expected editor= in --list-settings'
grep -Fxq 'save=on' "$tmp/out" \
    || fail 'expected save=on in --list-settings'
grep -Fxq "config-file=$conf_shown" "$tmp/out" \
    || fail 'expected config-file= in --list-settings'
cut -d= -f1 "$tmp/out" | LC_ALL=C sort -c \
    || fail 'expected --list-settings sorted by name'
"$POWWOW" --no-config --list-settings --get editor > /dev/null 2>&1 \
    && fail 'expected --list-settings and --get to fail together'
name='--set host creates a definition file'
"$POWWOW" --no-config --set 'host=mud.example 4000 --no-tls' \
    "$tmp/new.pw" > "$tmp/out" 2>&1 || fail '--set host failed'
grep -Fxq '#host mud.example 4000 --no-tls --utf8' "$tmp/new.pw" \
    || fail 'expected #host in the new definition file'
[ "$("$POWWOW" --no-config --get host "$tmp/new.pw" 2> /dev/null)" \
  = 'mud.example 4000 --no-tls --utf8' ] \
    || fail 'expected --get host to print the host'
name='--get prints the paths of the user-wide settings file and the manual'
[ "$("$POWWOW" "--config=$conf" --get config-file 2> /dev/null)" \
  = "$conf_shown" ] \
    || fail 'expected --get config-file to print the user-wide settings file'
[ -z "$("$POWWOW" --no-config --get config-file 2> /dev/null)" ] \
    || fail 'expected --get config-file to print nothing with --no-config'
manual=$("$POWWOW" --no-config --get manual 2> /dev/null)
case $manual in
    */powwow.md) [ -f "$manual" ] || fail "expected the manual at $manual" ;;
    *) fail "expected --get manual to print the path of powwow.md: $manual" ;;
esac
"$POWWOW" --no-config --set manual=x > /dev/null 2>&1 \
    && fail 'expected --set manual to fail'
name='--get and --set need no terminal, and save the keys of xterm then'
rm -f "$tmp/new.pw"
env -u TERM "$POWWOW" --no-config --set 'host=mud.example 4000 --no-tls' \
    "$tmp/new.pw" > "$tmp/out" 2>&1 || fail '--set without $TERM failed'
[ "$(env -u TERM "$POWWOW" --no-config --get host "$tmp/new.pw" \
     2> /dev/null)" = 'mud.example 4000 --no-tls --utf8' ] \
    || fail 'expected --get without $TERM to print the host'
[ "$(grep -Ec '^#bind (Up|Down|Left|Right|Home|End|Delete) ' \
     "$tmp/new.pw")" -eq 7 ] \
    || fail 'expected the cursor keys and Delete in a file made by --set'
grep -Fxq '#bind Up ^[OA=&prev-line' "$tmp/new.pw" \
    || fail 'expected the Up of xterm in a file made by --set without $TERM'
grep -Fxq '#bind C-a ^A=&begin-of-line' "$tmp/new.pw" \
    || fail 'expected the other default keys in a file made by --set'
# and those of $TERM, if it has an entry
if command -v tic > /dev/null 2>&1; then
    printf 'pwcsi|xterm with CSI cursor keys,\n\tkcuu1=\\E[A, use=xterm,\n' \
        > "$tmp/pwcsi.ti"
    if TERMINFO=$tmp/terminfo tic -x "$tmp/pwcsi.ti" 2> /dev/null; then
        rm -f "$tmp/new.pw"
        TERM=pwcsi TERMINFO=$tmp/terminfo "$POWWOW" --no-config \
            --set 'host=mud.example 4000 --no-tls' "$tmp/new.pw" \
            > /dev/null 2>&1 || fail '--set with $TERM failed'
        grep -Fxq '#bind Up ^[[A=&prev-line' "$tmp/new.pw" \
            || fail 'expected the Up of $TERM in a file made by --set'
    fi
fi
name='--get and --set fail for unknown settings and invalid values'
"$POWWOW" --no-config --get nosuch > "$tmp/out" 2> /dev/null \
    && fail 'expected --get nosuch to fail'
[ -s "$tmp/out" ] && fail 'expected nothing on standard output'
"$POWWOW" --no-config --set save=maybe "$tmp/new.pw" > /dev/null 2>&1 \
    && fail 'expected --set save=maybe to fail'

run '#secret add deletes the lines and words with the secret' \
    "#put say hunter22
#put keep this
#add hunter22x hunter21x
#secret add
hunter22
#history
#file =$tmp/purged.pw
#option +words
#save"
history
lacks_history hunter22
has_history 'keep this'
grep -q 'hunter22' "$tmp/purged.pw" && fail 'the secret is in the file'
grep -q 'hunter21x' "$tmp/purged.pw" \
    || fail 'expected the other words in the file'

run --eof --until '(secret to add, not shown): ' \
    '#secret add shows a label instead of the input line' '#secret add'
grep -Fq '(secret to add, not shown): ' "$tmp/out" \
    || fail 'expected the label'

run 'Tab completes the subcommands of #secret, and #secret add numbers them' \
    '#secret add
hunter21' "#secret ad	
hunter22
"
has '#secret number 1 added.'
has '#secret number 2 added.'

run '#secret: a secret entered is left as <redacted>' \
    '#hilite messages bold
#secret add
hunter22
say password=hunter22def'
grep -Fq 'say password=<ESC>[1m<redacted><ESC>[mdef' "$tmp/out" \
    || fail 'expected only the secret as <redacted>, drawn as the messages'
lacks 'hunter22def'

run '#secret: Backspace deletes, and moving the cursor does nothing' \
    "#secret add
hunter23$(printf '\010')2
#secret delete
hunter22
#secret add
hunt$(printf '\002')er22
#secret delete
hunter22"
[ "$(grep -c '^#secret deleted\.$' "$tmp/out")" -eq 2 ] \
    || fail 'expected both secrets to be deleted'

cx8=$(printf '\0308\r')
cx8e=$(printf '\0308ee')
run '#secret: &insert-char and &insert-emoji add characters' "#secret add
hunt${cx8}u+e9
r22${cx8e}thumbs up
${cx8}nosuch
$(printf '\007')
#secret delete
hunter22
#secret delete
hunt${cx8}u+e9
r22
#secret delete
hunt${cx8}u+e9
r22${cx8e}thumbs up

#secret"
# the input line, erased with CR and ESC [ K, is before the messages
grep -q '#secret number 1 added\.$' "$tmp/out" \
    || fail 'expected the secret added'
[ "$(grep -c '#no such secret\.$' "$tmp/out")" -eq 2 ] \
    || fail 'expected two secrets not found'
grep -q '#secret deleted\.$' "$tmp/out" || fail 'expected the secret deleted'
has '#no secrets.'

run '#secret: C-g cancels, and a short secret is added with a warning' \
    "#secret add
hunt$(printf '\007')
#secret add
abcd
#secret"
has '#secret: cancelled.'
warning='#warning: a secret shorter than 8 bytes may match other text,'
has "$warning and is easy to find from its hash"
has '#1: 4 bytes'
lacks '#2: 4 bytes'

run '#secret delete and its errors' '#secret add
hunter22
#secret add
hunter22
#secret add
abc
#secret add

#secret delete
hunter23
#secret delete
hunter22
#secret
#secret add
hunter22
#secret delete 2
#secret delete 1
#secret delete x
#secret nosuch
#secret =0123'
has '#secret already kept.'
has '#secret too short: at least 4 bytes.'
has '#secret: cancelled.'
has '#no such secret.'
has '#secret deleted.'
has '#no secrets.'
has '#no secret 2.'
has '#secret 1 deleted.'
has '#syntax: #secret [add|delete [<number>]|=<hash>]'
has '#secret: invalid hash "0123"'

run '#secret keeps lines out of #record' "#record $tmp/secret.rec
#secret add
hunter22
#print (\"hunter\" + \"22\")
#print (\"kept\")
#record"
grep -q 'hunter22' "$tmp/secret.rec" && fail 'the secret is in the file'
grep -Fq 'kept' "$tmp/secret.rec" || fail 'expected the other lines'

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

run 'without UTF-8, the copyright sign, the dashes and the quotes are ASCII' \
    '#print ("x" + *169 + *8211 + *8212 + *8216 + *8217 + *8220 + *8221 + "y")'
grep -Fq 'x(c)--'"''"'""y' "$tmp/out" || fail "expected x(c)--''\"\"y"

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

run 'a window of the default size uses its last column' '' \
    "$(printf '%080d\025' 0)"
grep -Fq "$(printf '%080d' 0)" "$tmp/raw" \
    || fail "wrapped before the last column"

run --one-by-one 'typing at the end draws just the characters typed' '' \
    "abcd$(printf '\025')"
grep -Fq 'bcd' "$tmp/raw" || fail "moved the cursor between the characters"

# C-b C-f move the cursor, so that the next character redraws the line
run 'the cursor stays in the last column after blanking it' '' \
    "$(printf '%078d\002\006' 0)b$(printf '\025')"
grep -Eq 'b <ESC>\[[0-9]+;80H' "$tmp/out" \
    || fail "left the cursor after the blank in the last column"

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

run 'a mark puts the colors of the text back after it' '#mark foo=red
#print ("\033[32ma foo b\033[m")'
has '<ESC>[32ma <ESC>[31mfoo<ESC>[m<ESC>[32m b<ESC>[m'

# in a terminal with RGB, up to the attr operator
COLORTERM=truecolor
export COLORTERM
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
has '#syntax: #hilite [input|messages] [<attribute>|off|default]'
has '  example:  bold yellow on blue'

run 'bright colors with a capital first letter' '#mark foo=White on BLUE
#mark bar=bold Red
#mark'
has '#mark foo=White on Blue'
has '#mark bar=bold Red'

run 'attr operator' '#print (attr "dim #ff8700" + "x")'
has '<ESC>[2;38;2;255;135;0mx'
unset COLORTERM

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

# with TERM=xterm, Up is ^[OA; a terminal that ignores application cursor
# mode sends ^[[A, which works too, unless it is bound itself
run 'the cursor keys also work in their other form' '' \
    "#print one
${esc}[A
#bind Up2 ^[[B=#print two
${esc}[B
"
count=$(awk '$0 == "one"' "$tmp/out" | wc -l)
[ "$count" -eq 2 ] || fail "expected one twice, got $count"
has 'two'

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

# a search that a line starts, typed so that it starts after the line
run '#key C-r searches from an empty line, which C-g leaves empty' \
    "$history" "#key C-r
$(printf '\007')#print restored
"
grep -Fxq 'restored' "$tmp/out" \
    || fail 'expected C-g to leave the line empty, not bring back #key C-r'

run '#keyedit &reverse-search finds lines as C-r does' "$history" \
    "#keyedit &reverse-search
ban${ce}s
"
has 'bananas'

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
    name='--version shows the copyright sign in UTF-8'
    "$POWWOW" --version > "$tmp/out" 2>&1
    grep -Fq '© 1998–' "$tmp/out" || fail 'expected © 1998–'
    run 'capitalize and downcase non-ASCII words' '' \
        "#print ǆUNGLA ΟΔΟΣ 12ab${esc}b${esc}b${esc}b${esc}c${esc}c${esc}c!
"
    has 'ǅungla Οδος 12Ab!'

    # C-b and C-f draw the e first, so that the accent joins its cell
    run 'a combining character typed after its base draws them again' '' \
        "$(printf 'e\002\006\314\201\025')"
    grep -Eq "<ESC>\\[[0-9;]*He$(printf '\314\201')" "$tmp/out" \
        || fail "e and the accent not drawn again together"

    run 'a combining character at the start of the line is drawn on a space' \
        '' "$(printf '\314\201a\025')"
    grep -Eq "<ESC>\\[[0-9;]*H $(printf '\314\201')" "$tmp/out" \
        || fail "the accent not drawn on a space"

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

# padding such as $<2> is left out of the sequences of the terminal
if command -v tic > /dev/null 2>&1; then
    printf 'pwpad|xterm with padding,\n\tel=\\E[K$<1>$<2>, use=xterm,\n' \
        > "$tmp/pwpad.ti"
    if TERMINFO=$tmp/terminfo tic -x "$tmp/pwpad.ti" 2>/dev/null; then
        TERM=pwpad TERMINFO=$tmp/terminfo
        export TERMINFO
        run 'padding is left out of terminal sequences' '#print hello'
        has 'hello'
        grep -Fq '$<' "$tmp/raw" && fail 'padding written to the terminal'
        TERM=xterm
        unset TERMINFO
    fi
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

run 'walk sequences repeat a direction at most 1000 times' '#map 99999999999n
#map 1001s
#map 3e
#map'
has '#map: 3e'

run --bind 'rebind rejects an empty key sequence' \
    '#bind foo=bar' "$esc[20~" "#rebind foo =
"
has '#rebind: empty key sequence.'

run '#rebindall rejects arguments other than --every' '#rebindall --all'
has '#syntax: #rebindall [--every]'

# the line completed is printed, not run: the keys typed after a running
# #rebindall would be read as the keys to rebind
run 'Tab completes #rebindall --every' '' \
    "#rebindall --e	$(printf '\001')#print 
"
grep -q '^#rebindall --every *$' "$tmp/out" || fail 'expected --every'

run --eof '#rebindall --every asks for every key' '#rebindall --every'
grep -Fq '#please press the key "C-x-8-Ret"' "$tmp/out" \
    || fail 'expected C-x-8-Ret'

run --eof '#rebindall skips the keys typed after a control key' '#rebindall'
grep -Fq '#please press the key "C-x-8-Ret"' "$tmp/out" \
    && fail 'expected no C-x-8-Ret'
grep -Fq '#please press the key "Up" : ' "$tmp/out" || fail 'expected Up'

# the rest of a key that other keys start with, typed after the timeout
name='#rebind waits for the rest of a key after a prefix such as C-x'
: > "$tmp/raw"
{
    printf '#rebind C-x-8-e-e\n'
    wait_for '#please press the key'
    printf '\030'
    sleep 0.3
    printf '8ex'
    wait_for '#redefined key'
    printf '#quit\n'
} | "$POWWOW" > "$tmp/raw" 2>&1
sed -e "s/$esc/<ESC>/g" -e "s/$cr//g" "$tmp/raw" > "$tmp/out"
has '#redefined key: C-x-8-e-e ^X8ex=&insert-emoji'

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

run 'repeating a text too many times is an error' \
    '#setvar mem=0
#print ("ab" * 2147483648)
#($s = "ab")
#($s *= 2147483648)
#print ("s=" + $s)
#setvar mem=100
#print ("ab" * 4611686018427387904)'
has '#print: #error: operand or index out of range.'
has '#error: operand or index out of range.'
has 's=ab'
has '#print: #error: text/string longer than limit, discarded.'

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

run 'percent of a text too large for a number is an error' \
    '#print ("n " + %(%(" -12abc")))
#print (%("99999999999999999999"))
#print (%("-99999999999999999999"))'
has 'n -12'
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

    # without the fix, fails when built with -ftrapv
    run 'adding, subtracting and multiplying wrap around' \
        '#(@x = 9223372036854775807, @m = -@x - 1)
#print ("a " + %(@x + 1) + " " + %(@m - 1) + " " + %(@x * 2) + " " + %-@m)
#(@a = @x, @b = @m, @c = @x, @d = @x, @e = @m)
#(@a += 1, @b -= 1, @c *= 2, @d++, @e--)
#print ("b " + %@a + " " + %@b + " " + %@c)
#print ("c " + %@d + " " + %@e)
#(@a = @x, @b = @m)
#print ("d " + %(++@a) + " " + %(--@b))'
    has 'a -9223372036854775808 9223372036854775807 -2 -9223372036854775808'
    has 'b -9223372036854775808 9223372036854775807 -2'
    has 'c -9223372036854775808 9223372036854775807'
    has 'd -9223372036854775808 9223372036854775807'
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
has "Execute a speedwalk sequence even if the 'speedwalk' option is disabled."

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

run '#(expression) needs its closing parenthesis' '#(@1 = 1
#(@1 = (2)
#(@1 = 3;#print ("not run")'
[ "$(grep -c '^#error: missing right parenthesis\.$' "$tmp/out")" -eq 3 ] \
    || fail 'expected three missing parentheses'
lacks 'not run'

run '#(expression) warns about text after it' '#(1)x
#(@1 = 2) 
# (3)\;(y)
#print (@1)'
has '#warning: text after the expression ignored: "x"'
has '#warning: text after the expression ignored: "\;(y)"'
[ "$(grep -c '^#warning: text after' "$tmp/out")" -eq 2 ] \
    || fail 'expected no warning about the spaces after an expression'
has '2'

# the greeting has the attributes of the messages, as the warnings after it
printf '#hilite messages bold\n' > "$tmp/.config/powwow/powwow.conf"
run 'Type #help for help. is shown as a message' ''
grep -Fq '<ESC>[1mType #help for help.' "$tmp/out" \
    || fail 'expected Type #help for help. in the attributes of messages'
no_hilite
# but not with a definition file
printf '#alias greet=1\n' > "$tmp/greet.pw"
run --args "$tmp/greet.pw" 'a definition file has no greeting' ''
grep -Fq 'Type #help' "$tmp/out" && fail 'unexpected greeting'

# a session locks its definition file: another session of it warns, and
# does not save it on exit unless the first has quit by then, but #save
# saves it
printf '#alias base=1\n#init =#print ("<rea" + "dy>")\n' > "$tmp/lock.pw"
# a session in the background, until the file $tmp/lock-quit exists
start_first() {
    rm -f "$tmp/lock-quit" "$tmp/first.out"
    {
        until [ -e "$tmp/lock-quit" ]; do sleep 0.05; done
        printf '%s\n#quit\n' "$1"
    } | "$POWWOW" "$tmp/lock.pw" > "$tmp/first.out" 2>&1 &
    first=$!
    i=0
    until grep -q '<ready>' "$tmp/first.out" 2>/dev/null \
            || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
}
start_first '#alias first=1'
run --args "$tmp/lock.pw" 'a second session of a definition file warns' \
    '#alias second=1'
grep -Fq 'is in use by another powwow (pid' "$tmp/out" \
    || fail 'expected a warning that the file is in use by another powwow'
has '#definition file not saved: it is in use by another powwow'
grep -q 'second' "$tmp/lock.pw" && fail 'the second session saved the file'
run --args "$tmp/lock.pw" '#save saves a definition file open elsewhere' \
    '#alias saved=1
#save'
grep -q 'saved' "$tmp/lock.pw" || fail '#save did not save the file'
touch "$tmp/lock-quit"
wait $first
grep -q 'first' "$tmp/lock.pw" || fail 'the first session did not save'
run --args "$tmp/lock.pw" 'a session after the first has quit saves' \
    '#alias third=1'
grep -Fq 'in use by another powwow' "$tmp/out" && fail 'unexpected warning'
grep -q 'third' "$tmp/lock.pw" || fail 'the session did not save'
name='a second session saves if the first has quit before it'
start_first ''
rm -f "$tmp/second-quit"
{
    until [ -e "$tmp/second-quit" ]; do sleep 0.05; done
    printf '#alias later=1\n#quit\n'
} | "$POWWOW" "$tmp/lock.pw" > "$tmp/second.out" 2>&1 &
second=$!
i=0
until grep -q '<ready>' "$tmp/second.out" 2>/dev/null || [ $i -ge 500 ]
do
    sleep 0.01
    i=$((i + 1))
done
touch "$tmp/lock-quit"
wait $first
touch "$tmp/second-quit"
wait $second
grep -q 'later' "$tmp/lock.pw" || fail 'the second session did not save'
# --set refuses a definition file open in a session, which would write
# over it, and --get in-use prints the pid of that session
name='--set refuses a definition file open in powwow'
start_first ''
[ "$("$POWWOW" --no-config --get in-use "$tmp/lock.pw")" = "$first" ] \
    || fail 'expected --get in-use to print the pid of the session'
"$POWWOW" --no-config --set wrap=on "$tmp/lock.pw" > "$tmp/out" 2>&1 \
    && fail 'expected --set to fail on a file open in powwow'
grep -Fq "is in use by a running powwow (pid $first); quit it first" \
    "$tmp/out" || fail "unexpected message: $(cat "$tmp/out")"
touch "$tmp/lock-quit"
wait $first
[ -z "$("$POWWOW" --no-config --get in-use "$tmp/lock.pw")" ] \
    || fail 'expected --get in-use to print nothing after the session'
"$POWWOW" --no-config --set wrap=on "$tmp/lock.pw" > /dev/null 2>&1 \
    || fail 'expected --set to work after the session'

# with --config, a user-wide setting changed in a session is saved in the
# user-wide settings file at once, and the session reads that file again
# when another program, such as the launcher, writes a new one
name='a session saves the user-wide settings at once, and reads them again'
printf '#alias base=1\n#option +info\n' > "$tmp/live.pw"
rm -f "$tmp/live.conf" "$tmp/live-go" "$tmp/live.out"
{
    printf '#option editor=from-session\n'
    until [ -e "$tmp/live-go" ]; do sleep 0.05; done
    # once a second at most, so that it is seen after a second
    sleep 1.2
    printf '#option list\n#hilite messages default\n#quit\n'
} | "$POWWOW" "--config=$tmp/live.conf" "$tmp/live.pw" > "$tmp/live.out" \
    2>&1 &
live=$!
i=0
until grep -q 'editor is now' "$tmp/live.out" 2>/dev/null || [ $i -ge 500 ]
do
    sleep 0.01
    i=$((i + 1))
done
grep -Fxq '#option editor=from-session' "$tmp/live.conf" \
    || fail 'expected the editor in the user-wide settings file at once'
# as the launcher writes it: a new file in its place
printf '#savefile-version 9\n#option pager=from-file\n#hilite messages bold\n' \
    > "$tmp/live.new"
mv "$tmp/live.new" "$tmp/live.conf"
touch "$tmp/live-go"
wait $live
sed -e "s/$esc\[[0-9;?]*[A-Za-z]//g" "$tmp/live.out" | tr -d '\r' \
    > "$tmp/out"
grep -q '^#reloaded user-wide settings from' "$tmp/out" \
    || fail 'expected the user-wide settings file to be read again'
sed -n '/^#option list/,$p' "$tmp/out" > "$tmp/list"
grep -Fq '#option pager=from-file' "$tmp/list" \
    || fail 'expected the pager of the new user-wide settings file'
grep -Fq '#option editor=from-session' "$tmp/list" \
    && fail 'expected the editor of the old file to be gone'
grep -Fxq '#option pager=from-file' "$tmp/live.conf" \
    || fail 'expected the session not to save over the new file'
grep -Fxq '#message highlighting is now on (user-wide default).' "$tmp/out" \
    || fail 'expected the #hilite of the new user-wide settings file'

# "# " starts a comment, up to the next ;, typed, in a definition file and
# in a file that #exe reads
printf '%s\n' '#savefile-version 9' '#reset bind' '# a comment; #alias good=x' \
    '#' '  # indented' > "$tmp/comments"
printf '%s\n' '# a comment, "not run"' '# (@1 = 5)' \
    '# then; #print ("run" + %@1)' > "$tmp/cmds"
run 'comments end at a semicolon' \
    "#load $tmp/comments
#alias
#exe <$tmp/cmds
# xyz;#print (\"typed\")"
has '#alias good@*=x'
has 'run5'
lacks 'not run'
has 'typed'

# a key bound with its sequence replaces the key with that sequence
run '#bind with a sequence replaces the key with that sequence' \
    '#bind ^S ^S=#capture
#bind edit'
has '#deleting key binding: C-s ^S=&forward-search'
has '#new key binding: ^S ^S=#capture'
lacks '#bind C-s ^S=&forward-search'

# a save-file older than the default editing keys gets them after its own
# keys, but not over those it binds
printf '%s\n' '#savefile-version 1' '#bind C-k ^K=&clear-line' > "$tmp/old"
run 'the default keys of a very old save-file do not double its keys' \
    "#load $tmp/old
#bind edit"
has '#bind C-k ^K=&clear-line'
lacks '#bind C-k ^K=&kill-to-eol'

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

run '#reset bind --missing adds the default keys not bound' \
    '#bind C-x-8-Ret=
#bind KP8=
#bind M-c ^[c=&upcase-word
#bind foo ^[[20~=bar
#reset bind --missing
#reset bind --missing
#bind edit
#bind'
has '#default keys added: C-x-8-Ret, KP8'
has '#no default keys are missing.'
has '#bind C-x-8-Ret ^X8^M=&insert-char'
has '#bind M-c ^[c=&upcase-word'
has '#bind foo ^[[20~=bar'

run '#reset rejects unknown lists' '#bind foo ^[[20~=bar
#reset bind 
#bind foo
#reset nosuch'
has '#no such key: "foo"'
has '#reset: unknown list "nosuch", must specify one of:'

# a delayed message, as text from the MUD, draws the input line again,
# and the keys of an incomplete sequence after it
name='the keys typed so far are shown again after a message'
: > "$tmp/raw"
{
    printf '#in x (2000) #print ("lat" + "er")\n'
    sleep 0.3
    printf '\030'
    wait_for later
    sleep 0.3
    printf '\007#quit\n'
} | "$POWWOW" > "$tmp/raw" 2>&1
sed -n '/later/,$p' "$tmp/raw" | grep -q 'C-x-' \
    || fail 'expected C-x- after the message'

# resumed after C-z, in a shell with job control in tmux, the input line
# and the cursor are at the bottom, also after the line took two rows and
# then one again
if command -v tmux > /dev/null 2>&1; then
    name='the cursor is on the input line after C-z and fg'
    tm() { env -u TMUX tmux -L "powwow-test-$$" -f /dev/null "$@"; }
    tm_wait() {
        i=0
        until tm capture-pane -p -t t | grep -Fq -e "$1" || [ $i -ge 500 ]
        do
            sleep 0.01
            i=$((i + 1))
        done
    }
    case $POWWOW in
        /*) pw=$POWWOW ;;
        *) pw=$PWD/$POWWOW ;;
    esac
    tm new-session -d -s t -x 80 -y 24 \
        "env -i HOME=$tmp TERM=xterm PATH=/usr/bin:/bin PS1='shell> ' sh -i"
    tm_wait 'shell>'
    tm send-keys -t t -l "$pw --no-config"
    tm send-keys -t t Enter
    tm_wait '#help'
    # a prompt, as from the MUD, which a line that took two rows starts
    tm send-keys -t t -l "#emulate !printf 'mud-%s> ' prompt"
    tm send-keys -t t Enter
    tm_wait 'mud-prompt> '
    line=$(printf 'x%.0s' 1 2 3 4 5 6 7 8 9 10 11 12)
    line=$line$line$line$line$line
    tm send-keys -t t -l "${line}yyyyyyyyyyyyyyy"
    tm_wait yyyyy
    for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        tm send-keys -t t BSpace
    done
    tm send-keys -t t C-a C-z
    tm_wait Stopped
    # not without job control
    if tm capture-pane -p -t t | grep -q Stopped; then
        tm send-keys -t t -l fg
        tm send-keys -t t Enter
        tm_wait 'shell> fg'
        sleep 0.5
        at=$(tm display -p -t t '#{cursor_x},#{cursor_y}')
        [ "$at" = 12,23 ] || fail "the cursor at $at, not 12,23"
    fi
    tm kill-server 2>/dev/null
fi

run '#help &<function> shows an editing function and its keys' \
    '#help &abort
#help &nosuch'
has '&abort                  stop searching (C-g)'
has '#no such editing function: "&nosuch"'

run '#key and #keyedit without an argument show their syntax' '#key
#keyedit'
has '#syntax: #key <name>'
has '#syntax: #keyedit <function>'

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
#option editor=vi
#option
#save $tmp/cfg"
has '<ESC>[1m#no substitutions are defined.<ESC>[m'
has '<ESC>[1m#beep<ESC>[m'
grep -q '^<ESC>\[1m#option +autoclear -autoprint .*<ESC>\[m$' "$tmp/out" \
    || fail 'expected #option to list the options as messages'
has '<ESC>[1m#option editor=vi<ESC>[m'

run '#option help-pager shows #help, instead of #option pager' \
    '#option pager=echo pager
#option help-pager=echo help-pager
#help alias
#option help-pager=
#help alias'
grep -q 'help-pager /' "$tmp/out" || fail 'expected the help pager'
grep -Eq '(^|>)pager /' "$tmp/out" || fail 'expected the pager once cleared'

POWWOWHELPPAGER='echo help-env'
export POWWOWHELPPAGER
run '$POWWOWHELPPAGER shows #help, after #option help-pager' \
    '#option pager=echo pager
#help alias
#option help-pager=echo help-pager
#help alias'
unset POWWOWHELPPAGER
grep -q 'help-env /' "$tmp/out" || fail 'expected $POWWOWHELPPAGER'
grep -q 'help-pager /' "$tmp/out" || fail 'expected the help pager'
grep -Eq '(^|>)pager /' "$tmp/out" && fail 'expected no pager'

run '#option list aligns the descriptions, also after a long command' \
    '#option editor=&"C:\\Program Files\\Notepad++\\notepad++.exe" -multiInst
#option help-pager=-
#option pager=cat
#option shell=sh
#option list
#option help-pager=
#option pager=
#option shell='
has '#option +autoclear       clear input line before executing commands'
# with the quotes as ASCII, as the locale is not UTF-8
has "#option help-pager=-     program for showing '#help'"
has "#option pager=cat        program for showing texts"
has "#option shell=sh         program for running '#!' and '!'"
has '#option editor=&"C:\\Program Files\\Notepad++\\notepad++.exe" -multiInst'
has "#                        program for editing texts"
has '#options, not saved without a definition file:'
lacks '#user-wide options'
grep -Fxq '#hilite input red' "$tmp/cfg" || fail "#hilite input not saved"
grep -Fxq '#hilite messages bold' "$tmp/cfg" || fail "#hilite messages not saved"

# for a command option that is not set, #option list shows the command
# used instead, and where it comes from
unset POWWOWEDITOR POWWOWHELPPAGER POWWOWSHELL
EDITOR=nano
export EDITOR
run '#option list shows the commands used for command options not set' \
    '#option list'
has '#option editor=nano (from $EDITOR)'
has '#option help-pager=cat (from $POWWOWPAGER)'
has '#option pager=cat (from $POWWOWPAGER)'
has '#option shell=/bin/sh (the default)'
POWWOWPAGER= PAGER=
export PAGER
run 'an empty variable of the environment is no command' '#option list'
has '#option pager=more (the default)'
POWWOWPAGER=cat
unset PAGER
run 'clearing a command option says what its default is' \
    '#option editor=vi
#option editor=
#option shell=
#option +info'
has '#option editor is now the default: nano (from $EDITOR)'
has '#option shell is now the default: /bin/sh'
run --args --windows '--windows: #option list names variables as %NAME%' \
    '#option list'
has '#option editor=nano (from %EDITOR%)'
has '#option pager=cat (from %POWWOWPAGER%)'
unset EDITOR

# with --windows, the default editor and pager are Notepad, or nano in an
# ssh session, and the shell powwow-cmd, if they are found in PATH; not
# in the ssh session that the tests may be run in
mkdir "$tmp/winbin"
for f in notepad.exe nano powwow-cmd; do
    printf '#! /bin/sh\n' > "$tmp/winbin/$f"
    chmod +x "$tmp/winbin/$f"
done
unset POWWOWPAGER SSH_CONNECTION
saved_path=$PATH
PATH=$tmp/winbin:$PATH
run --args --windows '--windows: Notepad and powwow-cmd by default' \
    '#option list'
has '#option editor=&notepad.exe (the default)'
has '#option pager=&notepad.exe (the default)'
has '#option help-pager=&notepad.exe (the default)'
has '#option shell=powwow-cmd (the default)'
SSH_CONNECTION='192.0.2.1 50000 192.0.2.2 22'
export SSH_CONNECTION
run --args --windows '--windows: nano by default in an ssh session' \
    '#option list'
has '#option editor=nano (the default)'
has '#option pager=nano -v (the default)'
unset SSH_CONNECTION
run 'no Windows defaults without --windows' '#option list'
has '#option editor=vi (the default)'
has '#option pager=more (the default)'
has '#option shell=/bin/sh (the default)'
PATH=$saved_path
run --args --windows '--windows: no Windows defaults not found in PATH' \
    '#option list'
has '#option editor=vi (the default)'
has '#option shell=/bin/sh (the default)'
POWWOWPAGER=cat
export POWWOWPAGER

run --args --windows '--windows: #help names variables as %NAME%' \
    '#help option'
has "others; they come before '%POWWOWEDITOR%', '%POWWOWPAGER%',"

printf '#alias kept=yes\n' > "$tmp/groups.pw"
printf '#hilite input none\n#hilite messages none\n' > "$tmp/groups.conf"
run --args "--config=$tmp/groups.conf $tmp/groups.pw" \
    '#option list groups the options by the file that they are saved in' \
    '#option list'
has "#user-wide options, saved in '$tmp/groups.conf':"
has "#options of this MUD, saved in '$tmp/groups.pw':"
grep -B1 '^#options of this MUD' "$tmp/out" | head -n 1 | grep -q '^$' \
    || fail 'expected an empty line between the groups'
sed -n '/^#user-wide/,/^#options of/p' "$tmp/out" > "$tmp/user-wide"
grep -q '^#option +save ' "$tmp/user-wide" || fail 'save is not user-wide'
grep -q '^#option editor=' "$tmp/user-wide" || fail 'editor is not user-wide'
grep -q '^#option +autoclear ' "$tmp/user-wide" \
    && fail 'autoclear is user-wide'
run --args "$tmp/groups.pw" \
    '#option list: without --config, all are saved in the definition file' \
    '#option list'
has "#options, saved in '$tmp/groups.pw':"
lacks '#user-wide options'
lacks '#options of this MUD'
# nor in #option list
dir="$tmp/Application Support/powwow with a long name"
run --args "--config=$tmp/groups.conf" \
    '#option list does not break a file name at its spaces' \
    "#file =$dir/mume.powwow
#option list"
has "#    '$dir/mume.powwow':"

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
has '#help [-P|--no-pager] [keys|math|comment|<command>|&<function>]'
has '#addstatic {<text>|(<expr>)}    add the string to the static completion list'
lacks "Help on 'help'"

# - as the pager of #help is none, from the option or the environment
POWWOWPAGER='echo PAGER USED #'
run '#option help-pager=- is no pager' '#option help-pager=-
#help help'
lacks 'PAGER USED'
has '#help [-P|--no-pager] [keys|math|comment|<command>|&<function>]'
lacks "Help on 'help'"
POWWOWHELPPAGER=-
export POWWOWHELPPAGER
run '$POWWOWHELPPAGER=- is no pager' '#help help'
unset POWWOWHELPPAGER
lacks 'PAGER USED'
has '#help [-P|--no-pager] [keys|math|comment|<command>|&<function>]'
POWWOWPAGER=cat

# run from the build directory, powwow finds the help in the source tree
unset POWWOWHELP
run 'help from the source tree' '#help -P keys'
POWWOWHELP=${srcdir:-.}/../doc
export POWWOWHELP
has 'C-x 8 Ret   &insert-char'

# moved with its data, powwow finds the help beside it: in share/powwow
# next to its bin, and in Resources next to the MacOS of an app bundle
for layout in bin:share/powwow Contents/MacOS:Contents/Resources; do
    rm -rf "$tmp/moved"
    mkdir -p "$tmp/moved/${layout%%:*}" "$tmp/moved/${layout#*:}"
    cp "$POWWOW" "$tmp/moved/${layout%%:*}/powwow"
    printf '@keys\nhelp beside the program\n' \
        > "$tmp/moved/${layout#*:}/powwow.help"
    unset POWWOWHELP
    saved=$POWWOW
    POWWOW=$tmp/moved/${layout%%:*}/powwow
    run "help beside the program in ${layout#*:}" '#help -P keys'
    POWWOW=$saved
    POWWOWHELP=${srcdir:-.}/../doc
    export POWWOWHELP
    has 'help beside the program'
done

run 'Tab completes commands and their arguments' '' \
    "#he	speedw	
#reset b	C-x-8-e			$(printf '\025')
"
has "Execute a speedwalk sequence even if the 'speedwalk' option is disabled."
has 'C-x-8-e-e  C-x-8-e-s'

run 'Tab lists the commands right after completing part of the name' '' \
    "#rebi		$(printf '\025')
"
grep -aq '#rebind  *#rebindall' "$tmp/out" || fail "expected the list"

run 'Tab completes #connect main, the address of #host and its port' \
    '#host 127.0.0.1 1 --no-tls' "#connect m	12	--no		
" '#history
'
grep -Eq '^# *[0-9]+: #connect main 127\.0\.0\.1 --no-tls 1 $' "$tmp/out" \
    || fail 'expected the line completed'

run 'Tab completes the address of #host as the first argument of #connect' \
    '#host 127.0.0.1 1 --no-tls' "#connect 12		
" '#history
'
grep -Eq '^# *[0-9]+: #connect 127\.0\.0\.1 1 $' "$tmp/out" \
    || fail 'expected the line completed'

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
# keys read separately are drawn after moving the cursor where they go
sed 's/<ESC>\[[0-9;]*H//g' "$tmp/out" | grep -aq 'fights off $' \
    || fail "expected #group fights off"

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
sed 's/<ESC>\[[0-9;]*H//g' "$tmp/out" > "$tmp/drawn"
grep -aq '#alias kfoo $' "$tmp/drawn" || fail "expected #alias kfoo"
grep -aq '#action -hpcheck $' "$tmp/drawn" || fail "expected #action -hpcheck"
grep -aq '#mark troll $' "$tmp/drawn" || fail "expected #mark troll"
grep -aq '#var \$target $' "$tmp/drawn" || fail "expected #var \$target"

run 'the spaces after a file name are not part of it' "#save $tmp/saved "
[ -f "$tmp/saved" ] || fail "expected $tmp/saved"

mkdir -p "$tmp/defs/sub"
echo '#print ("load" + "ed")' > "$tmp/defs/mud.powwow"
run 'Tab completes file names, and directories without a space' '' \
    "#load $tmp/def	mu	
#capture >$tmp/defs/s	
#save $tmp/defs/		$(printf '\025')
"
has 'loaded'
has "#error writing file '$tmp/defs/sub/'"
has 'mud.powwow  sub/'

# file names in any case where the file system ignores case, as on macOS
mkdir -p "$tmp/case"
echo '#print ("upper" + "case")' > "$tmp/case/Mud.powwow"
echo '#print ("lower" + "case")' > "$tmp/case/mob.powwow"
run 'Tab completes file names in any case where the system ignores it' \
    '' "#load $tmp/case/mu	
#load $tmp/case/M		$(printf '\025')
"
if [ -e "$tmp/case/mud.powwow" ]; then
    has 'uppercase'
    has 'mob.powwow  Mud.powwow'
else
    lacks 'uppercase'
fi

# a file name is the rest of the line, so Tab completes past its spaces
mkdir -p "$tmp/with space"
echo '#print ("lo" + "aded2")' > "$tmp/with space/mud.powwow"
run 'Tab completes file names with spaces' '' \
    "#load $tmp/with sp	mu	
#print <$tmp/with sp	mu	
"
has 'loaded2'
has '#print ("lo" + "aded2")'

# with --windows, Tab completes file names in any case, as on Windows,
# folders with a \ after them, and a drive; here, the \ is part of the name
# of the file
mkdir -p "$tmp/win/Some Dir"
run --args --windows \
    '--windows: Tab completes file names in any case, with a \' '' \
    "#capture $tmp/win/SOME D	x.txt
#capture
#print <C:	nonexistent
"
[ -f "$tmp/win/Some Dir\\x.txt" ] \
    || fail "expected $tmp/win/Some Dir\\x.txt, got: $(ls "$tmp/win")"
has "#print: #error opening 'C:\\nonexistent'"

run 'Tab completes the arguments of #hilite' '' \
    "#hil	inp	un	Ye	o	cy	
#hil	me	b		
"
grep -aq 'input highlighting is now <ESC>\[4;93;46mon' "$tmp/out" \
    || fail "expected underline Yellow on cyan"
has 'black  blink  blue   bold'

# RGB as the terminal shows colors: as it is with COLORTERM=truecolor, or
# else the nearest of the named colors, as xterm has 8
COLORTERM=truecolor
export COLORTERM
run '#hilite in RGB where the terminal has RGB' '#hilite input #ff8700'
grep -aq 'input highlighting is now <ESC>\[38;2;255;135;0mon' "$tmp/out" \
    || fail 'expected the RGB of #ff8700'
unset COLORTERM
run '#hilite in RGB as the nearest named color' '#hilite input #ff8700'
grep -aq 'input highlighting is now <ESC>\[33mon' "$tmp/out" \
    || fail 'expected yellow, the named color nearest to #ff8700'

run 'Tab lists the words down and then across' '' \
    "#option 		$(printf '\025')
"
has 'autoclear    echo         history      list         sendsize     wrap'
has 'debug        help-pager=  keyecho      save         words'

name='powwow-movieplay -a converts lines longer than 4 KB'
long=$(printf '%05000d' 0)
printf 'line %s\nsleep 10\nprompt >\n' "$long" > "$tmp/movie"
if "$(dirname "$POWWOW")/powwow-movieplay" -a "$tmp/movie" > "$tmp/out" 2>&1
then
    [ "$(cat "$tmp/out")" = "$long
>" ] || fail 'the movie is not converted'
else
    fail "exit status $?"
fi

# the texts that #help shows must be ASCII, except for what tty_put_wc()
# writes as ASCII where the locale is not UTF-8: the copyright sign, the
# dashes U+2010 to U+2015, and the quotation marks U+2018 to U+201F
name='the texts of #help have no other non-ASCII characters'
copyright=$(printf '\302\251')
dash=$(printf '\342\200[\220-\225]')
quote=$(printf '\342\200[\230-\237]')
high=$(printf '[\200-\377]')
: > "$tmp/out"
for f in "${srcdir:-.}/../COPYING" "${srcdir:-.}/../doc/unicode-license.txt" \
         "${srcdir:-.}/../doc/powwow.help"; do
    LC_ALL=C sed -e "s/$copyright//g" -e "s/$dash//g" -e "s/$quote//g" "$f" \
        | LC_ALL=C grep -n "$high" | sed "s|^|${f##*/}:|" >> "$tmp/out"
done
[ -s "$tmp/out" ] && fail "replace these, or write them as ASCII in tty_put_wc()"

# the help fits in a terminal of 80 columns, each of those characters in
# one, as awk counts bytes in some locales
name='the lines of #help are below 80 columns'
LC_ALL=C sed -e "s/$copyright/c/g" -e "s/$dash/-/g" -e "s/$quote/'/g" \
    "${srcdir:-.}/../doc/powwow.help" \
    | awk 'length > 79 { print "powwow.help:" FNR ": " $0 }' > "$tmp/out"
[ -s "$tmp/out" ] && fail "wrap these lines"

# --completion: the script that completes the arguments of powwow, run
# as bash and zsh run it: complete_in SHELL LINE prints the completions
# of LINE in $tmp/compl, each followed by |, which shows the space after
# it; in zsh, whose own completers complete files and hosts, also the
# calls of those, of compadd and of compset
mkdir -p "$tmp/compl/dir" "$tmp/compl/defs"
: > "$tmp/compl/mume.powwow"
: > "$tmp/compl/notes.txt"
: > "$tmp/compl/defs/arda.powwow"
cat > "$tmp/compl-stubs.zsh" <<'END'
typeset -A compstate
compadd() {
    local suffix=
    while [[ $1 == -* ]]; do
        case $1 in
            -S) suffix=$2; shift 2 ;;
            --) shift; break ;;
            *) shift ;;
        esac
    done
    local word
    for word; do
        print -r -- "$word$suffix|"
        (( compstate[nmatches]++ ))
    done
}
_files() { print -r -- "_files${*:+ $*}|"; }
_hosts() { print -r -- "_hosts${*:+ $*}|"; }
_command_names() { print -r -- "_command_names${*:+ $*}|"; }
compset() { print -r -- "compset $*|"; }
END
complete_in() {
    case $1 in
        bash)
            bash --norc --noprofile -c '. "$1"
                COMP_LINE=$2 COMP_POINT=${#2}
                cd "$3" && _powwow powwow "${2##*[ =:]}" ""
                [ ${#COMPREPLY[@]} -eq 0 ] \
                    || printf "%s|\n" "${COMPREPLY[@]}"' \
                bash "$tmp/compl.bash" "$2" "$tmp/compl" ;;
        zsh)
            zsh -f -c 'autoload -U +X compinit && compinit -u -D
                . "$1"
                . "$4"
                words=(${(z)2})
                [[ $2 == *" " ]] && words+=("")
                CURRENT=${#words} PREFIX=${words[-1]}
                cd "$3" && _powwow_zsh_complete' \
                zsh "$tmp/compl.zsh" "$2" "$tmp/compl" \
                "$tmp/compl-stubs.zsh" ;;
    esac 2>&1
}
# expect_completion SHELL LINE COMPLETION...: exactly these completions
expect_completion() {
    shell=$1 line=$2
    shift 2
    : > "$tmp/expected"
    [ $# -eq 0 ] || printf '%s|\n' "$@" > "$tmp/expected"
    complete_in "$shell" "$line" > "$tmp/out"
    cmp -s "$tmp/expected" "$tmp/out" \
        || fail "$shell completes '$line' as: $(tr '\n' ' ' < "$tmp/out")"
}
name='--completion of another shell fails'
"$POWWOW" --completion fish > "$tmp/out" 2>&1 \
    && fail 'expected --completion fish to fail'
grep -Fq 'no completion for fish; only for bash and zsh' "$tmp/out" \
    || fail "expected the shells: $(cat "$tmp/out")"
for shell in bash zsh; do
    command -v $shell > /dev/null 2>&1 || continue
    name="--completion $shell"
    "$POWWOW" --completion $shell > "$tmp/compl.$shell" \
        || fail "--completion $shell failed"
    $shell -n "$tmp/compl.$shell" || fail "not valid for $shell"
    # eval without the double quotes gets the script on one line: it
    # says so, fails and defines nothing; with them, it says nothing
    (cd "$tmp/compl/dir" && $shell -c 'eval $(cat "$1") && echo succeeded
        command -v _powwow > /dev/null && echo defined' \
        $shell "$tmp/compl.$shell") > "$tmp/out" 2>&1
    grep -Fq "write eval \"\$(powwow --completion $shell)\" with the" \
        "$tmp/out" && ! grep -q -e defined -e succeeded "$tmp/out" \
        || fail "unquoted eval in $shell: $(cat "$tmp/out")"
    if [ $shell = bash ]; then
        bash --norc --noprofile -c 'eval "$(cat "$1")"' bash \
            "$tmp/compl.bash"
    else
        zsh -f -c 'autoload -U +X compinit && compinit -u -D
            eval "$(cat "$1")"' zsh "$tmp/compl.zsh"
    fi > "$tmp/out" 2>&1
    [ -s "$tmp/out" ] && fail "quoted eval in $shell: $(cat "$tmp/out")"
    # in zsh, the whole word is replaced; in bash, the part after = or :
    if [ $shell = bash ]; then
        set=
        tls=
    else
        set=save=
        tls=--tls=
    fi
    expect_completion $shell 'powwow --ver' '--version '
    expect_completion $shell 'powwow --tls=no' "${tls}no-verify "
    expect_completion $shell 'powwow --get ed' 'editor '
    expect_completion $shell 'powwow --get conf' 'config-file '
    expect_completion $shell 'powwow --set sa' 'save='
    expect_completion $shell 'powwow --set conf'
    expect_completion $shell 'powwow --set save=o' "${set}on " "${set}off "
    expect_completion $shell 'powwow --completion ' 'bash ' 'zsh '
    # the definition files end with .powwow, unless none matches
    if [ $shell = bash ]; then
        expect_completion bash 'powwow d' 'defs/' 'dir/'
        expect_completion bash 'powwow mu' 'mume.powwow '
        expect_completion bash 'powwow ' 'defs/' 'dir/' 'mume.powwow '
        expect_completion bash 'powwow n' 'notes.txt '
        expect_completion bash 'powwow --config=di' 'dir/'
        expect_completion bash 'powwow --get save mu' 'mume.powwow '
    else
        expect_completion zsh 'powwow mu' '_files -g *.powwow(-.)'
        expect_completion zsh 'powwow mume.powwow lo' '_hosts'
        expect_completion zsh 'powwow --config=di' 'compset -P *=' '_files'
        expect_completion zsh 'powwow --get save mu' '_files -g *.powwow(-.)'
        expect_completion zsh 'powwow --set editor=vi' 'compset -P *=' \
            '_command_names -e'
    fi
    POWWOWDIR=$tmp/compl/defs
    export POWWOWDIR
    if [ $shell = bash ]; then
        expect_completion bash 'powwow ar' 'arda.powwow '
    else
        expect_completion zsh 'powwow ar' '_files -g *.powwow(-.)' \
            "_files -W $POWWOWDIR -g *.powwow(-.)"
    fi
    unset POWWOWDIR
    case $(uname -s) in
        CYGWIN*) expect_completion $shell 'powwow --wi' '--windows ' ;;
        *) expect_completion $shell 'powwow --wi' ;;
    esac
    # the settings are those of --list-settings
    complete_in $shell 'powwow --get ' | sed 's/ |$//' > "$tmp/out"
    "$POWWOW" --no-config --list-settings 2> /dev/null | sed 's/=.*//' \
        | cmp -s - "$tmp/out" \
        || fail 'expected the settings of --list-settings'
done

# in a zsh that completes as one typing in it does, by zpty: a file in
# HOME by ~, which stays as typed; a key of its own writes the line, as
# completed, and the output is read, up to what is typed after it, as zsh
# waits for that
name='--completion zsh completes ~/ in zsh'
if command -v zsh > /dev/null 2>&1 \
   && zsh -f -c 'zmodload zsh/zpty' 2> /dev/null; then
    cat > "$tmp/zpty.zsh" <<'END'
zmodload zsh/zpty
zpty -b z 'PS1="> " zsh -f -i'
zpty -w z "autoload -U compinit && compinit -u -D; . ${(q)1}"
zpty -w z "dump() { print -r -- \$BUFFER > ${(q)3}; }; zle -N dump"
zpty -w z 'bindkey -e; bindkey "^Xd" dump'
zpty -w -n z "$2"$'\t\C-xd\C-e!END!\n'
zpty -r z out '*!END!*'
zpty -d z
END
    rm -f "$tmp/zbuf"
    HOME=$tmp/compl zsh -f "$tmp/zpty.zsh" "$tmp/compl.zsh" 'powwow ~/mu' \
        "$tmp/zbuf" > /dev/null 2>&1
    [ "$(cat "$tmp/zbuf" 2> /dev/null)" = 'powwow ~/mume.powwow ' ] \
        || fail "completed as: '$(cat "$tmp/zbuf" 2> /dev/null)'"
fi

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
# file names in quotes, as ASCII, as the locale is not UTF-8
has "#settings saved to '$tmp/defs.cfg'."
has "#end of capture to '$tmp/cap'."
has "#end of movie to '$tmp/mov'."
has "#end of record to '$tmp/rec'."

long=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
run 'messages wrap before the words that do not fit' "#alias $long"
has '#unknown alias, cannot show:'
has "#    \"$long\""

run 'help on a command without an entry' '#help nosuchcommand'
has '#no entry for "nosuchcommand" in the help file.'

run 'error message' '#connect test --no-tls 127.0.0.1 1'
has '#cannot connect to 127.0.0.1:1: Connection refused'

run '#connect <host> <port> needs no session id' \
    '#connect --no-tls 127.0.0.1 1'
has '#cannot connect to 127.0.0.1:1: Connection refused'

run '#connect without an argument connects to the host of #host' \
    '#host 127.0.0.1 1 --no-tls
#connect'
has '#cannot connect to 127.0.0.1:1: Connection refused'

# MPI messages longer than a read, with openssl as the MUD, if powwow has
# TLS: the prompt before them, which the newlines before them do not end,
# is drawn again after each; and the prompt at the end of the TLS record
# with the end of one, which GnuTLS has read and select() does not see,
# is drawn at once
if command -v openssl > /dev/null 2>&1 \
   && openssl req -x509 -newkey rsa:2048 -nodes -subj /CN=localhost \
        -days 1 -keyout "$tmp/key.pem" -out "$tmp/cert.pem" \
        > /dev/null 2>&1; then
    port=$((20000 + $$ % 20000))
    x=$(printf '%020000d' 0)
    {
        sleep 2
        printf 'mud-prompt>\377\371'
        sleep 0.5
        printf '\r\n~$#EV20007\nstats\n%s\n' "$x"
        sleep 0.5
        printf '\r\n~$#EV20007\nstats\n%s\n' "$x"
        sleep 0.5
        printf 'second-prompt>\377\371'
        sleep 0.5
        printf '\r\n~$#EV20007\nstats\n%s\nnext-prompt>\377\371' "$x"
        # open until powwow gives up waiting for the prompt, as closing
        # the connection would show it
        sleep 15
    } | openssl s_server -quiet -naccept 1 -accept "$port" \
            -cert "$tmp/cert.pem" -key "$tmp/key.pem" > /dev/null 2>&1 &
    server=$!
    sleep 1
    run --until 'next-prompt>' 'the prompts around long MPI messages' \
        "#option pager=&true
#hilite messages bold
#connect main 127.0.0.1 $port --tls=no-verify
#request editor"
    kill $server 2>/dev/null
    wait $server 2>/dev/null
    if grep -q "#connecting to 127.0.0.1:$port (TLS)" "$tmp/out"; then
        all='#read all 20007 chars\.'
        m="$all.*mud-prompt>.*$all.*mud-prompt>.*$all.*second-prompt>"
        m="$m.*next-prompt>"
        tr -d '\n' < "$tmp/out" \
            | grep -q "$m" \
            || fail 'expected the prompts after the messages'
        grep -q "<ESC>\\[1m$all" "$tmp/out" \
            || fail 'expected read all as a message'
    fi
fi

# after a pager that powwow waits for, here cat for an MPI view from
# openssl as the MUD, in tmux, the input line and the cursor are at the
# bottom, also after the line took two rows and then one again
if command -v tmux > /dev/null 2>&1 && [ -f "$tmp/cert.pem" ]; then
    name='the cursor is on the input line after a pager'
    port=$((20000 + ($$ + 1) % 20000))
    {
        sleep 2
        printf 'mud-prompt> \377\371'
        sleep 3
        printf '\r\n~$#EV%d\nview\n' 765
        for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 \
                 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40
        do
            printf 'view line %08d\n' "$i"
        done
        sleep 15
    } | openssl s_server -quiet -naccept 1 -accept "$port" \
            -cert "$tmp/cert.pem" -key "$tmp/key.pem" > /dev/null 2>&1 &
    server=$!
    sleep 1
    tm new-session -d -s t -x 80 -y 24 \
        "env -i HOME=$tmp TERM=xterm PATH=/usr/bin:/bin POWWOWPAGER=cat $pw --no-config"
    tm_wait '#help'
    tm send-keys -t t -l "#connect main 127.0.0.1 $port --tls=no-verify"
    tm send-keys -t t Enter
    tm_wait 'connected!'
    if tm capture-pane -p -t t | grep -q '(TLS)'; then
        tm send-keys -t t -l '#request editor'
        tm send-keys -t t Enter
        # tmux leaves out the space at the end
        tm_wait 'mud-prompt>'
        line=$(printf 'x%.0s' 1 2 3 4 5 6 7 8 9 10 11 12)
        line=$line$line$line$line$line
        tm send-keys -t t -l "${line}yyyyyyyyyyyyyyy"
        tm_wait yyyyy
        for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
            tm send-keys -t t BSpace
        done
        tm send-keys -t t C-a
        tm_wait 'view line 00000040'
        # drawn again, after the view scrolled it away
        tm_wait 'mud-prompt> xxxxx'
        sleep 0.2
        at=$(tm display -p -t t '#{cursor_x},#{cursor_y}')
        [ "$at" = 12,23 ] || fail "the cursor at $at, not 12,23"
    fi
    tm kill-server 2>/dev/null
    kill $server 2>/dev/null
    wait $server 2>/dev/null
fi

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

run 'action pattern with an escaped $ and &' \
    '#action >a ^$1 costs \\$5 \\& more=#print bought $1
#action >b ^\\${target} leaves $1=#print follow $1
#emulate bread costs $5 & more
#emulate ${target} leaves north'
has 'bought bread'
has 'follow north'

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

# --windows keeps the backslashes of a file name after <, as path
# separators on Windows; elsewhere than on Cygwin, where they are path
# separators too, they are part of the name of the file
case $(uname -s) in
    CYGWIN*)
        cygwin=1
        wtmp=$(cygpath -w "$tmp")       # what %NAME% gives for $tmp
        mkdir "$tmp/back"
        printf 'from the file\n' > "$tmp/back/slash" ;;
    *)
        cygwin=
        wtmp=$tmp
        printf 'from the file\n' > "$tmp/back\\slash" ;;
esac
run --args --windows '--windows: a file name after < keeps its backslashes' \
    "#print <$tmp/back\\slash"
has 'from the file'
run 'a file name after < loses its backslashes without --windows' \
    "#print <$tmp/back\\slash"
has "#print: #error opening '$tmp/backslash'"

# --windows replaces each %NAME% in a file name by the value of the
# environment variable, as cmd does, but not one that is not set
POWWOW_TEST_DIR=$tmp
export POWWOW_TEST_DIR
run --args --windows '--windows: %NAME% in file names' \
    "#print <%POWWOW_TEST_DIR%/back\\slash
#capture %POWWOW_TEST_DIR%/cap.txt
#capture
#save %POWWOW_TEST_DIR%/saved.pw
#print <%POWWOW_TEST_UNSET%/x"
has 'from the file'
has "#end of capture to '$wtmp/cap.txt'."
has "#settings saved to '$wtmp/saved.pw'."
has "#print: #error opening '%POWWOW_TEST_UNSET%/x'"
[ -e "$tmp/cap.txt" ] && [ -e "$tmp/saved.pw" ] \
    || fail 'expected the files in the directory of %POWWOW_TEST_DIR%'
# with --windows, a backslash in the name of the definition file is a
# folder, so the file is not in POWWOWDIR
mkdir "$tmp/pwdir"
[ -z "$cygwin" ] || mkdir -p a "$tmp/pwdir/a"
POWWOWDIR=$tmp/pwdir/
export POWWOWDIR
run --args --windows \
    '--windows: a backslash is a folder in the name of the definition file' \
    '#save a\b.pw'
has "#settings saved to 'a\\b.pw'."
rm -rf 'a\b.pw' a
run 'a backslash is part of the name of the definition file' '#save a\b.pw'
has "#settings saved to '$tmp/pwdir/a\\b.pw'."
unset POWWOWDIR

run 'no %NAME% in file names without --windows' \
    '#print <%POWWOW_TEST_DIR%/x'
has "#print: #error opening '%POWWOW_TEST_DIR%/x'"

# a line longer than the memory limit is still processed as a line, not
# again as a prompt
awk 'BEGIN { s = sprintf("%150s", ""); gsub(/ /, "x", s); print s
             print "after" }' > "$tmp/long"
run 'line longer than the memory limit' "#setvar mem=120
#emulate <$tmp/long"
has '#error: text/string longer than limit, discarded.'
has 'after'
lacks '#out of memory'

# a prompt longer than the memory limit
awk 'BEGIN { s = sprintf("%150s", ""); gsub(/ /, "x", s); printf "%s", s }' \
    > "$tmp/long"
run 'prompt longer than the memory limit' "#setvar mem=120
#emulate <$tmp/long"
has '#error: text/string longer than limit, discarded.'
lacks '#out of memory'

# a prompt that #mark makes longer than the memory limit, followed by
# the rest of its line
awk 'BEGIN { s = sprintf("%90s", ""); gsub(/ /, "x", s); printf s " rest" }' \
    > "$tmp/long"
run 'marked prompt longer than the memory limit' "#setvar mem=120
#mark x=bold
#prompt >a ^xxx=#isprompt 90
#prompt >b rest=#print (\"after\")
#emulate <$tmp/long"
has '#error: text/string longer than limit, discarded.'
grep -q 'after$' "$tmp/out" || fail 'the second #prompt did not run'

# powwow alone in its session, as under "xterm -e powwow", has no shell
# to resume it
if python3 -c "" > /dev/null 2>&1; then
    cat > "$tmp/setsid.py" <<'EOF'
import os, sys
os.setsid()
os.execv(sys.argv[1], sys.argv[1:])
EOF
    powwow=$POWWOW
    POWWOW=python3
    run --args "$tmp/setsid.py $powwow" \
        'C-z says that powwow cannot be suspended without job control' \
        '' "$(printf '\032')"
    POWWOW=$powwow
    has '#cannot suspend: powwow was not started by a shell with job control.'
fi

# the fake MUDs below need python3 to run: on Windows, the python3 found
# can be an alias that only offers to install it
if python3 -c "" > /dev/null 2>&1; then
    # a MUD that asks for a password in no-echo mode, and keeps the first
    # line it gets in the file $2
    cat > "$tmp/server.py" <<'EOF'
import os, socket, sys
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
with open(sys.argv[1] + '.new', 'w') as f:
    f.write(str(s.getsockname()[1]))
os.rename(sys.argv[1] + '.new', sys.argv[1])
c, _ = s.accept()
c.sendall(b'\xff\xfb\x01Password: ')
got = b''
while b'\n' not in got:
    data = c.recv(1024)
    if not data:
        break
    got += data
with open(sys.argv[2], 'wb') as f:
    f.write(got)
c.sendall(b'\xff\xfc\x01\r\nWelcome\r\n')
c.close()
EOF
    name='no-echo mode sends the line edited, without the keys bound'
    rm -f "$tmp/port" "$tmp/got"
    python3 -I "$tmp/server.py" "$tmp/port" "$tmp/got" &
    server=$!
    i=0
    until [ -s "$tmp/port" ] || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
    : > "$tmp/raw"
    {
        printf '#connect m --no-tls 127.0.0.1 %s\n' "$(cat "$tmp/port")"
        wait_for 'Password: '
        # Backspace deletes, Left and C-a do nothing, and &insert-char and
        # &insert-emoji add characters
        printf 'hunter2X\177%s[D\0012\0308\ru+e9\r\0308eethumbs up\r\r' \
            "$esc"
        wait_for 'Welcome'
        printf '#quit\n'
    } | "$POWWOW" > "$tmp/raw" 2>&1
    wait $server
    sed -e "s/$esc/<ESC>/g" "$tmp/raw" > "$tmp/out"
    printf '\377\375\001hunter22\303\251\360\237\221\215\n' > "$tmp/expected"
    cmp -s "$tmp/got" "$tmp/expected" \
        || fail "unexpected line: $(od -c "$tmp/got")"
    grep -q 'hunter' "$tmp/out" && fail 'the password was shown'
    grep -Fq '(insert-emoji) name: ' "$tmp/out" \
        || fail 'expected the name of the emoji to be read'

    # a MUD that answers three lines, each with a prompt
    cat > "$tmp/server.py" <<'EOF'
import os, socket, sys, time
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
with open(sys.argv[1] + '.new', 'w') as f:
    f.write(str(s.getsockname()[1]))
os.rename(sys.argv[1] + '.new', sys.argv[1])
c, _ = s.accept()
got = b''
c.sendall(b'HP:0>')
while got.count(b'\n') < 3:
    data = c.recv(1024)
    if not data:
        break
    got += data
for answer in (b'X.', b'You say it.', b'A room.'):
    c.sendall(b'\r\n' + answer + b'\r\nHP:>')
    time.sleep(0.2)
c.sendall(b'\r\nBye\r\n')
c.close()
EOF
    name='#option reprint shows a line with a secret as <redacted>'
    rm -f "$tmp/port"
    python3 -I "$tmp/server.py" "$tmp/port" &
    server=$!
    i=0
    until [ -s "$tmp/port" ] || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
    : > "$tmp/raw"
    {
        printf '#secret add\nhunter22\n#option +reprint\n'
        printf '#prompt >p ^HP:=#isprompt\n'
        printf '#connect m --no-tls 127.0.0.1 %s\n' "$(cat "$tmp/port")"
        wait_for 'connected!'
        printf 'x\rsay hunter22\rlook\r'
        wait_for 'Bye'
        printf '#quit\n'
    } | "$POWWOW" > "$tmp/raw" 2>&1
    wait $server
    sed -e "s/$esc\[[0-9;?]*[A-Za-z]//g" -e "s/$cr//g" "$tmp/raw" \
        > "$tmp/out"
    has 'HP:>(say <redacted>)'
    has 'HP:>(look)'
    grep -q 'hunter22' "$tmp/out" && fail 'the secret was reprinted'

    # a MUD that sends a prompt, and later another, with no newline between
    cat > "$tmp/server.py" <<'EOF'
import os, socket, sys, time
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
with open(sys.argv[1] + '.new', 'w') as f:
    f.write(str(s.getsockname()[1]))
os.rename(sys.argv[1] + '.new', sys.argv[1])
c, _ = s.accept()
c.sendall(b'A room.\r\nMore> ')
time.sleep(0.5)
c.sendall(b'HP:>')
time.sleep(0.5)
c.sendall(b'\r\nBye\r\n')
c.close()
EOF
    name='#isprompt without a length draws the whole line as the prompt'
    rm -f "$tmp/port"
    python3 -I "$tmp/server.py" "$tmp/port" &
    server=$!
    i=0
    until [ -s "$tmp/port" ] || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
    : > "$tmp/raw"
    {
        printf '#prompt >p ^More>=#isprompt\n'
        printf '#connect m --no-tls 127.0.0.1 %s\n' "$(cat "$tmp/port")"
        wait_for 'Bye'
        printf '#quit\n'
    } | "$POWWOW" > "$tmp/raw" 2>&1
    wait $server
    sed -e "s/$esc\[[0-9;?]*[A-Za-z]//g" -e "s/$cr//g" "$tmp/raw" \
        > "$tmp/out"
    has 'More> '
    has 'HP:>'

    # a MUD that tells after how long it got a telnet NOP, if within 3
    # seconds; with $2, it speaks telnet, sending a NOP itself first
    cat > "$tmp/server.py" <<'EOF'
import os, select, socket, sys, time
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
with open(sys.argv[1] + '.new', 'w') as f:
    f.write(str(s.getsockname()[1]))
os.rename(sys.argv[1] + '.new', sys.argv[1])
c, _ = s.accept()
c.sendall((b'\xff\xf1' if len(sys.argv) > 2 else b'') + b'Hi\r\n')
start = time.time()
got = b''
while b'\xff\xf1' not in got and time.time() - start < 3:
    r, _, _ = select.select([c], [], [], 0.05)
    if r:
        data = c.recv(1024)
        if not data:
            break
        got += data
if b'\xff\xf1' in got:
    c.sendall(b'Got a NOP after %d s\r\n' % round(time.time() - start))
else:
    c.sendall(b'No NOP\r\n')
c.close()
EOF
    for telnet in yes ''; do
        rm -f "$tmp/port"
        python3 -I "$tmp/server.py" "$tmp/port" $telnet &
        server=$!
        i=0
        until [ -s "$tmp/port" ] || [ $i -ge 500 ]; do
            sleep 0.01
            i=$((i + 1))
        done
        : > "$tmp/raw"
        {
            printf '#setvar keepalive=1\n'
            printf '#connect m --no-tls 127.0.0.1 %s\n' "$(cat "$tmp/port")"
            wait_for 'NOP'
            printf '#quit\n'
        } | "$POWWOW" > "$tmp/raw" 2>&1
        wait $server
        sed -e "s/$cr//g" "$tmp/raw" > "$tmp/out"
        if [ -n "$telnet" ]; then
            name='#setvar keepalive sends a telnet NOP when idle'
            has 'Got a NOP after 1 s'
        else
            name='#setvar keepalive sends nothing to a MUD without telnet'
            has 'No NOP'
        fi
    done

    # a MUD that keeps what it gets up to the line "look" in the file $2;
    # the #init of a definition file written before #option identify,
    # { #identify ; look}, is that option and {look}, which identifies when
    # connecting, before #init runs
    cat > "$tmp/server.py" <<'EOF'
import os, socket, sys
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
with open(sys.argv[1] + '.new', 'w') as f:
    f.write(str(s.getsockname()[1]))
os.rename(sys.argv[1] + '.new', sys.argv[1])
c, _ = s.accept()
got = b''
while b'look\n' not in got:
    data = c.recv(1024)
    if not data:
        break
    got += data
with open(sys.argv[2], 'wb') as f:
    f.write(got)
c.sendall(b'Bye\r\n')
c.close()
EOF
    name='#option identify identifies before #init, as #init {#identify;...} did'
    rm -f "$tmp/port" "$tmp/got"
    python3 -I "$tmp/server.py" "$tmp/port" "$tmp/got" &
    server=$!
    i=0
    until [ -s "$tmp/port" ] || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
    printf '#savefile-version 9\n#host 127.0.0.1 %s --no-tls\n' \
        "$(cat "$tmp/port")" > "$tmp/identify.pw"
    printf '#init ={ #identify ; look}\n' >> "$tmp/identify.pw"
    : > "$tmp/raw"
    {
        wait_for 'Bye'
        printf '#save\n#quit\n'
    } | "$POWWOW" "$tmp/identify.pw" > "$tmp/raw" 2>&1
    wait $server
    printf '~$#EI1\n1look\n' > "$tmp/expected"
    cmp -s "$tmp/got" "$tmp/expected" \
        || fail "unexpected identification: $(od -c "$tmp/got")"
    grep -Fq "#init: '#identify' is now '#option +identify'" "$tmp/raw" \
        || fail 'expected the conversion of #init to be reported'
    grep -Fxq '#init ={look}' "$tmp/identify.pw" \
        || fail 'expected #init without #identify'
    grep -Eq '^#option .*[+]identify' "$tmp/identify.pw" \
        || fail 'expected #option +identify'

    # a MUD that echoes again before the password is typed, and asks again
    cat > "$tmp/server.py" <<'EOF'
import os, socket, sys, time
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
with open(sys.argv[1] + '.new', 'w') as f:
    f.write(str(s.getsockname()[1]))
os.rename(sys.argv[1] + '.new', sys.argv[1])
c, _ = s.accept()
c.sendall(b'\xff\xfb\x01Password: ')
time.sleep(0.5)
c.sendall(b'\xff\xfc\x01\r\nToo slow.\r\n\xff\xfb\x01Password again: ')
got = b''
while b'\n' not in got:
    data = c.recv(1024)
    if not data:
        break
    got += data
with open(sys.argv[2], 'wb') as f:
    f.write(got)
c.sendall(b'\xff\xfc\x01\r\nWelcome\r\n')
c.close()
EOF
    name='a password not ended when the MUD echoes again is forgotten'
    rm -f "$tmp/port" "$tmp/got"
    python3 -I "$tmp/server.py" "$tmp/port" "$tmp/got" &
    server=$!
    i=0
    until [ -s "$tmp/port" ] || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
    : > "$tmp/raw"
    {
        printf '#connect m --no-tls 127.0.0.1 %s\n' "$(cat "$tmp/port")"
        wait_for 'Password: '
        printf 'abc'
        wait_for 'Password again: '
        printf 'hunter22\r'
        wait_for 'Welcome'
        printf '#quit\n'
    } | "$POWWOW" > "$tmp/raw" 2>&1
    wait $server
    tr -d '\377\373\374\375\376\001' < "$tmp/got" > "$tmp/line"
    printf 'hunter22\n' > "$tmp/expected"
    cmp -s "$tmp/line" "$tmp/expected" \
        || fail "unexpected line: $(od -c "$tmp/got")"

    # powwow in a terminal of 80x24, connected to a MUD that asks for the
    # window size with DO NAWS if $3 is "ask"; the terminal then becomes
    # 100x30, and $2 gets what the MUD received
    cat > "$tmp/naws.py" <<'EOF'
import fcntl, os, pty, signal, socket, struct, sys, termios, threading, time
powwow, out, ask = sys.argv[1], sys.argv[2], sys.argv[3] == 'ask'
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
got = bytearray()
def mud():
    c, _ = s.accept()
    if ask:
        c.sendall(b'\xff\xfd\x1f')
    c.sendall(b'Welcome\r\n')
    while True:
        data = c.recv(1024)
        if not data:
            break
        got.extend(data)
server = threading.Thread(target=mud, daemon=True)
server.start()
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm'
    os.execv(powwow, [powwow])
def size(rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ,
                struct.pack('HHHH', rows, cols, 0, 0))
shown = bytearray()
def drain():
    # read what powwow prints, so that it does not block
    try:
        while True:
            data = os.read(fd, 4096)
            if not data:
                break
            shown.extend(data)
    except OSError:
        pass
threading.Thread(target=drain, daemon=True).start()
# wait, for at most 5 seconds, until done() is true
def wait_until(done):
    for _ in range(500):
        if done():
            return
        time.sleep(0.01)
size(24, 80)
wait_until(lambda: b'Type #help' in shown)
os.write(fd, b'#connect m --no-tls 127.0.0.1 %d\n' % s.getsockname()[1])
wait_until(lambda: b'Welcome' in shown)
size(30, 100)
os.kill(pid, signal.SIGWINCH)
# WILL NAWS and two window sizes, or a command run after the resize
if ask:
    wait_until(lambda: len(got) >= 21)
else:
    os.write(fd, b'#print ("<sy" + "nc>")\n')
    wait_until(lambda: b'<sync>' in shown)
os.write(fd, b'#quit\n')
os.waitpid(pid, 0)
server.join(5)
with open(out, 'wb') as f:
    f.write(got)
EOF
    # the window size, of $1 columns and $2 rows in octal, as NAWS sends it
    naws_size() {
        printf '\377\372\037\000\'"$1"'\000\'"$2"'\377\360'
    }
    name='a resize sends no window size to a MUD that did not ask for it'
    python3 -I "$tmp/naws.py" "$POWWOW" "$tmp/got" no
    [ -s "$tmp/got" ] && fail "unexpected bytes: $(od -c "$tmp/got")"
    name='a MUD that asks for the window size gets it, also after a resize'
    python3 -I "$tmp/naws.py" "$POWWOW" "$tmp/got" ask
    { printf '\377\373\037'; naws_size 120 030; naws_size 144 036; } \
        > "$tmp/expected"
    cmp -s "$tmp/got" "$tmp/expected" \
        || fail "unexpected bytes: $(od -c "$tmp/got")"

    # powwow in a terminal that hangs up, by closing it as when its window
    # is closed if $3 is "close", or else by SIGHUP alone, after a change
    # to the definition file $2; the exit status of powwow, or the signal
    # that killed it, goes in $4
    cat > "$tmp/hup.py" <<'EOF'
import os, pty, select, signal, sys, time
powwow, deffile, how, out = sys.argv[1:5]
pid, fd = pty.fork()
if pid == 0:
    os.environ['TERM'] = 'xterm'
    os.execv(powwow, [powwow, deffile])
shown = bytearray()
# read what powwow prints, for at most 5 seconds, until it has 'text'
def wait_for(text):
    end = time.time() + 5
    while text not in shown and time.time() < end:
        if select.select([fd], [], [], 0.1)[0]:
            try:
                shown.extend(os.read(fd, 4096))
            except OSError:
                break
wait_for(b'<ready>')
os.write(fd, b'#alias hup=#print ("hung " + "up")\n#print ("<sy" + "nc>")\n')
wait_for(b'<sync>')
if how == 'close':
    os.close(fd)
else:
    os.kill(pid, signal.SIGHUP)
_, status = os.waitpid(pid, 0)
with open(out, 'w') as f:
    f.write('exit %d' % os.WEXITSTATUS(status) if os.WIFEXITED(status)
            else 'signal %d' % os.WTERMSIG(status))
EOF
    for how in close kill; do
        name="a terminal that hangs up ($how) saves the definition file"
        printf '#alias base=1\n#init =#print ("<rea" + "dy>")\n' \
            > "$tmp/hup.pw"
        rm -f "$tmp/hup.status"
        python3 -I "$tmp/hup.py" "$POWWOW" "$tmp/hup.pw" $how \
            "$tmp/hup.status"
        [ "$(cat "$tmp/hup.status" 2>/dev/null)" = 'exit 0' ] \
            || fail "expected exit 0, got: $(cat "$tmp/hup.status")"
        grep -Fq '#alias hup=' "$tmp/hup.pw" \
            || fail 'expected the new alias to be saved'
    done
fi

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

# an unterminated line of a spawned command, then a read without text
cat > "$tmp/server" <<'EOF'
printf '#print ("a" + '
sleep 0.2
printf '\377\361'
sleep 0.2
printf '"b")\n'
EOF
run --until '#connection on "n" closed.' \
    'unterminated line of a spawned command over a read without text' \
    "#spawn n sh $tmp/server"
has 'ab'

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

# the server gets the message that cancels an editing session once
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n#cancel\n'
cat > "$1/got"
touch "$1/done"
EOF
run --until '#cancelled session' 'cancelling an editing session once' \
    "#option editor=&sh -c 'sleep 5'
#spawn n sh $tmp/server $tmp
##n #request editor"
i=0
until [ -e "$tmp/done" ] || [ $i -ge 500 ]; do
    sleep 0.01
    i=$((i + 1))
done
[ "$(grep -c '^C5$' "$tmp/got")" -eq 1 ] \
    || fail "expected one cancel message, got: $(od -c "$tmp/got")"

# an edited text longer than #setvar editmem cancels the session
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n'
cat > "$1/got"
touch "$1/done"
EOF
run --until '#cancelled session' 'edited text longer than #setvar editmem' \
    "#setvar editmem=1000
#option editor=&awk 'BEGIN { for (i = 0; i < 1001; ++i) print }' >
#spawn n sh $tmp/server $tmp
##n #request editor"
i=0
until [ -e "$tmp/done" ] || [ $i -ge 500 ]; do
    sleep 0.01
    i=$((i + 1))
done
has '#edited text too long, not sent'
[ "$(grep -c '^C5$' "$tmp/got")" -eq 1 ] \
    || fail "expected a cancel message, got: $(od -c "$tmp/got")"

run --args --windows '--windows: no #spawn' '#spawn n sh -c true'
has '#spawn: #error: not available with --windows'

# with --windows, where there is no #spawn, a fake MUD runs the script $1
# with the arguments after it, connected to the first connection to the
# port of 127.0.0.1 it writes in $tmp/port; it needs python3, as the fake
# MUDs above do
mud_server() {
    cat > "$tmp/mud.py" <<'EOF'
import os, socket, subprocess, sys
s = socket.socket()
s.bind(('127.0.0.1', 0))
s.listen(1)
with open(sys.argv[1] + '.new', 'w') as f:
    f.write(str(s.getsockname()[1]))
os.rename(sys.argv[1] + '.new', sys.argv[1])
c, _ = s.accept()
subprocess.run(['sh'] + sys.argv[2:], stdin=c.fileno(), stdout=c.fileno())
c.close()
EOF
    rm -f "$tmp/port"
    python3 -I "$tmp/mud.py" "$tmp/port" "$@" &
    server=$!
    i=0
    until [ -s "$tmp/port" ] || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
}

# an editor with Windows settings: with --windows, its byte order mark
# and the CRs of its CR LF line endings are not sent, but a CR without LF
# is; without --windows, the text is sent as it is
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n'
cat > "$1/got"
touch "$1/done"
EOF
if python3 -c "" > /dev/null 2>&1; then
    mud_server "$tmp/server" "$tmp"
    run --args --windows --until '#completed session' \
        '--windows: edited text with a BOM and CR LF' \
        "#option editor=&printf '\\357\\273\\277one\\r\\ntwo\\rthree\\r\\n' >
#connect n --no-tls 127.0.0.1 $(cat "$tmp/port")
##n #request editor"
    i=0
    until [ -e "$tmp/done" ] || [ $i -ge 500 ]; do
        sleep 0.01
        i=$((i + 1))
    done
    wait $server
    printf '~$#EE17\nE5\none\ntwo\rthree\n' > "$tmp/want"
    # after the rest of the identification, the message
    tail -c 25 "$tmp/got" | cmp -s "$tmp/want" - \
        || fail "expected no BOM, and LF for CR LF, got: $(od -c "$tmp/got")"
    rm -f "$tmp/done"
fi
run --until '#completed session' 'edited text sent as it is' \
    "#option editor=&printf '\\357\\273\\277one\\r\\n' >
#spawn n sh $tmp/server $tmp
##n #request editor"
i=0
until [ -e "$tmp/done" ] || [ $i -ge 500 ]; do
    sleep 0.01
    i=$((i + 1))
done
printf '~$#EE11\nE5\n\357\273\277one\r\n' > "$tmp/want"
tail -c 19 "$tmp/got" | cmp -s "$tmp/want" - \
    || fail "expected the text as written, got: $(od -c "$tmp/got")"

# with --windows, the editor runs as #! runs a command, with #option shell,
# and the path of the file in double quotes after it; a \\ in double quotes
# is a \, as for sh, and others are kept
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE18\nM5\nthe title\ntext\n'
cat > /dev/null
EOF
cat > "$tmp/shell" <<'EOF'
printf '%s\n%s\n' "$1" "$TITLE" > "${0%/*}/shell-got"
exec sh -c "$1"
EOF
if python3 -c "" > /dev/null 2>&1; then
    mud_server "$tmp/server"
    run --args --windows --until '#cancelled session' \
        '--windows: the editor runs with #option shell' \
        "#option shell=sh $tmp/shell
#option editor=&true \"a\\\\b\" c\\\\d
#connect n --no-tls 127.0.0.1 $(cat "$tmp/port")
##n #request editor"
    wait $server
    grep -q '^true "a\\b" c\\\\d ".*powwow-edit-5-[^"]*"$' "$tmp/shell-got" \
        || fail "expected the editor and the file, got: $(cat "$tmp/shell-got")"
    sed -n 2p "$tmp/shell-got" | grep -qx 'the title' \
        || fail "expected TITLE, got: $(cat "$tmp/shell-got")"
fi

# an editing session with a long description
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE1009\nM5\n'
awk 'BEGIN { for (i = 0; i < 1000; ++i) printf "d"; print "\ntext" }'
cat > /dev/null
EOF
run --until '#cancelled session' 'editing session with a long description' \
    "#option editor=&true
#spawn n sh $tmp/server
##n #request editor"
descr=$(awk 'BEGIN { for (i = 0; i < 1000; ++i) printf "d" }')
has "#cancelled session $descr (5)"

# cancelling a view, which has no connection to tell
run --until '#killed' 'cancelling a view' \
    "#option pager=&sh -c 'sleep 5'
#help mark
#cancel"
has "#killed \"Help on 'mark'\" (4294967295)"
lacks "#no open connections. Use '#connect <host> <port>' to open a"\
" connection."

# #cancel without an argument cancels all the editing sessions
run --until "#killed \"Help on 'mark'\"" 'cancelling all editing sessions' \
    "#option pager=&sh -c 'sleep 5'
#help mark
#help beep
#cancel"
has "#killed \"Help on 'mark'\" (4294967295)"
has "#killed \"Help on 'beep'\" (4294967295)"

# #cancel with the number of no editing session
run --until '#killed' 'cancelling an unknown editing session' \
    "#option pager=&sh -c 'sleep 5'
#help mark
#cancel 4294967294
#cancel"
has '#unknown editing session 4294967294'

# #cancel with an argument that is not a number of an editing session
run --until '#killed' 'cancelling with a wrong argument' \
    "#option pager=&sh -c 'sleep 5'
#help mark
#cancel foo
#cancel -1
#cancel 4294967295x
#cancel 99999999999999999999
#cancel"
has '#syntax: #cancel [<number>]'
has '#unknown editing session 99999999999999999999'
[ "$(grep -c '^#killed' "$tmp/out")" -eq 1 ] \
    || fail "expected the view to be killed once"

# #cancel on an editing session already cancelled, before it ends
run --until '#killed' 'cancelling an editing session twice' \
    "#option pager=&sh -c 'sleep 5'
#help mark
#cancel 4294967295; #cancel 4294967295; #cancel"
has '#editing session 4294967295 already cancelled'
has '#no editing sessions to cancel.'
[ "$(grep -c '^#killed' "$tmp/out")" -eq 1 ] \
    || fail "expected the view to be killed once"

# a suboption from one connection, split by a suboption from another one
cat > "$tmp/server" <<'EOF'
printf '\377\372\052\001;ISO-8859-1'
touch "$1/sent"
until [ -e "$1/done" ]; do
    sleep 0.01
done
printf '\377\360'
EOF
cat > "$tmp/other" <<'EOF'
until [ -e "$1/sent" ]; do
    sleep 0.01
done
printf '\377\372\143\377\360'
sleep 0.2
touch "$1/done"
EOF
run --until '#connection on "n" closed.' \
    'suboptions from two connections at the same time' \
    "#spawn n sh $tmp/server $tmp
#spawn o sh $tmp/other $tmp"
has '#charset: iso-8859-1 (negotiated)'

# - as the pager is none: a text that the MUD sends to view is printed as
# its other lines, from the option or the environment
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EV24\ntitle\nline one\nline two\n'
printf '#print after\n'
EOF
POWWOWPAGER='echo PAGER USED #'
run --until '#connection on "n" closed.' '#option pager=- is no pager' \
    "#option pager=-
#spawn n sh $tmp/server
##n #request editor"
lacks 'PAGER USED'
has 'line one'
has 'line two'
has 'after'
POWWOWPAGER=-
run --until '#connection on "n" closed.' '$POWWOWPAGER=- is no pager' \
    "#spawn n sh $tmp/server
##n #request editor"
POWWOWPAGER=cat
has 'line two'

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

# an MPI message longer than the buffer it starts in and than #setvar mem,
# and a text to edit longer than #setvar editmem, which is read but not
# edited, as the edited text would replace all of it; the session is
# cancelled
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE6000\nM5\ndescr\n'
awk 'BEGIN { for (i = 0; i < 5991; ++i) printf "x" }'
sleep 0.2
printf '~$#EE8001\nM6\ndescr\n'
awk 'BEGIN { for (i = 0; i < 7992; ++i) printf "x" }'
printf '\n#print after\n'
cat > "$1/got"
touch "$1/done"
EOF
run --until 'after' 'MPI messages and #setvar editmem' \
    "#setvar mem=1000
#setvar editmem=8000
#option editor=&wc -c <
#spawn n sh $tmp/server $tmp
##n #request editor"
i=0
until [ -e "$tmp/done" ] || [ $i -ge 500 ]; do
    sleep 0.01
    i=$((i + 1))
done
grep -q '^#editor: *5991$' "$tmp/out" || fail 'missing line: #editor: 5991'
w='#warning: text to edit longer than the 8000 chars of #setvar editmem,'
has "$w not edited"
grep -q '^#editor: *7992$' "$tmp/out" && fail 'unexpected line: #editor: 7992'
has 'after'
[ "$(grep -c '^C6$' "$tmp/got")" -eq 1 ] \
    || fail "expected a cancel message, got: $(od -c "$tmp/got")"

# a text to view longer than #setvar editmem is cut short, also with - as
# the pager
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EV226\ntitle\n'
awk 'BEGIN { for (i = 0; i < 22; ++i) printf "line %04d\n", i }'
printf '#print after\n'
EOF
run --until '#connection on "n" closed.' \
    'a text to view longer than #setvar editmem' \
    "#setvar editmem=106
#option pager=-
#spawn n sh $tmp/server
##n #request editor"
has '#warning: text to view cut short to the 106 chars of #setvar editmem'
has 'line 0009'
lacks 'line 0010'
has 'after'

# the connection closes before the end of an MPI message
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE100\nabc'
EOF
run --until '#connection on "n" closed.' 'MPI message cut short' \
    "#spawn n sh $tmp/server
##n #request editor"
has '#connection "n" closed in the middle of an MPI message'

# the same, after a part of the text that comes later than the header
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE100\n'
sleep 0.2
printf 'abc'
EOF
run --until '#connection on "n" closed.' \
    'MPI message cut short after a part of its text' \
    "#spawn n sh $tmp/server
##n #request editor"
has '#connection "n" closed in the middle of an MPI message'

# MPI messages whose header is split across writes, after \n\r, after
# other text, and in the introducer, the length and before the \n
cat > "$tmp/server" <<'EOF'
read -r identify
for parts in '\n\r~$#|EE15\nM1\ndescr1\ntext\n' \
             'x~$|#EE15\nM2\ndescr2\ntext\n' \
             '~|$#EE15\nM3\ndescr3\ntext\n' \
             '~$#E|E15\nM4\ndescr4\ntext\n' \
             '~$#EE1|5\nM5\ndescr5\ntext\n' \
             '~$#EE15\r|\nM6\ndescr6\ntext\n'; do
    printf "${parts%%|*}"
    sleep 0.2
    printf "${parts#*|}"
done
cat > /dev/null
EOF
run --until '#cancelled session descr6 (6)' \
    'MPI messages split across writes' \
    "#option editor=&true
#spawn n sh $tmp/server
##n #request editor"
has '#warning: ignored an MPI message that may be spoofed'
lacks '#cancelled session descr1 (1)'
lacks '#cancelled session descr2 (2)'
has '#cancelled session descr3 (3)'
has '#cancelled session descr4 (4)'
has '#cancelled session descr5 (5)'
has '#cancelled session descr6 (6)'

# an MPI header after text that a previous write left without a newline
cat > "$tmp/server" <<'EOF'
read -r identify
printf 'prompt> '
sleep 0.2
printf '~$#EE15\nM1\ndescr1\ntext\n\n~$#EE15\nM2\ndescr2\ntext\n'
cat > /dev/null
EOF
run --until '#cancelled session descr2 (2)' \
    'MPI message after the text of a previous write' \
    "#option editor=&true
#spawn n sh $tmp/server
##n #request editor"
has '[prompt> ~$#EE15]'
lacks '#cancelled session descr1 (1)'
has '#cancelled session descr2 (2)'

# the start of an MPI message header ends the last read of a connection
cat > "$tmp/server" <<'EOF'
read -r identify
printf 'x\n~$#EE1'
EOF
run --until '#connection on "n" closed.' \
    'start of an MPI message header at the end of a connection' \
    "#spawn n sh $tmp/server
##n #request editor"
has '#warning: ##n: unterminated line [~$#EE1]'
lacks '#warning: bad MPI message header'

# the command run when an editing session starts closes the connection
# that sent it, with more text in the same read
cat > "$tmp/server" <<'EOF'
read -r identify
printf 'x\n~$#EE14\nM5\ndescr\ntext\ny\n'
cat > /dev/null
EOF
run --until '#connection on "n" closed.' \
    'editing session start command closing its connection' \
    "#alias z=#zap n
#option editor=&true
#spawn n sh $tmp/server
##n #identify z"
has '#connection on "n" closed.'

# the command run when an editing session starts closes the connection
# that sent it, which ends the session
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n'
cat > /dev/null
EOF
run --until 'ended' 'editing session start command ending the session' \
    "#alias z=#zap n
#alias e=#print (\"end\" + \"ed\")
#option editor=true
#spawn n sh $tmp/server
##n #identify z e"
has '#aborted "descr" (5)'
lacks '#cancelled session descr (5)'

# the command run when an editing session starts closes the connection
# that sent it, and opens a new one in its place
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n#print ("left")\n'
cat > /dev/null
EOF
run --until '#connection on "n" closed.' \
    'editing session start command replacing its connection' \
    "#alias z={#zap n;#spawn n sleep 5}
#option editor=&true
#spawn n sh $tmp/server
##n #identify z"
lacks 'left'

# a command started by #spawn closes its own connection with a line,
# followed by more lines in the same read
cat > "$tmp/server" <<'EOF'
printf '#zap n\n#print ("left")\npart'
sleep 5
EOF
run --until '#connection on "n" closed.' \
    'command started by spawn closing its connection' \
    "#spawn n sh $tmp/server"
lacks 'left'

# the connection of an editing session closes while the editor runs
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n'
EOF
run --until 'ended' 'connection closing during an editing session' \
    "#alias s=#print start
#alias e=#print (\"end\" + \"ed\")
#option editor=&sh -c 'sleep 5'
#spawn n sh $tmp/server
##n #identify s e"
has '#aborted "descr" (5)'
lacks '#cancelled session descr (5)'

# the temporary file of an editing session is in $TMPDIR, which may have
# a space
mkdir "$tmp/t d"
cat > "$tmp/server" <<'EOF'
read -r identify
printf '~$#EE14\nM5\ndescr\ntext\n'
cat > /dev/null
EOF
TMPDIR="$tmp/t d"
export TMPDIR
run --until '#editor: text' 'temporary file in TMPDIR' \
    "#option editor=&sh -c '[ \"\${0%/*}\" = \"\$TMPDIR\" ] \
&& echo \"in TMPDIR \${0##*/}\" && cat \"\$0\"'
#spawn n sh $tmp/server
##n #request editor"
TMPDIR=$tmp
grep -Eq '^#editor: in TMPDIR powwow-edit-5-[^/]{6}(\.txt)?$' "$tmp/out" \
    || fail "the file is not in TMPDIR"

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
