#!/bin/sh
#
# check that text from the MUD split into several packets is drawn as if
# it came in one (needs tmux and python3, or is skipped)
#

command -v tmux > /dev/null 2>&1 || { echo 'SKIP: no tmux'; exit 77; }
command -v python3 > /dev/null 2>&1 || { echo 'SKIP: no python3'; exit 77; }
exec python3 -I "${srcdir:-.}/test_split.py" "${POWWOW:-./powwow}"
