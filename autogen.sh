#! /bin/sh

# Create empty ChangeLog if we are building from git tree
test -f ChangeLog || touch ChangeLog

autoreconf -v --install || exit 1

./configure "$@"
