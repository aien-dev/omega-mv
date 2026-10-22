#!/bin/sh
# Keep this inode adjacent to OUT_DIR: `make clean` removes OUT_DIR itself.
# Never unlink the lock file, even after release: waiters may hold its inode.
set -eu
out=${1:?output directory required}
shift
mkdir -p -- "$out"
canonical=$(cd -- "$out" && pwd -P)
lock=$canonical.omega-build.lock
refuse() { printf 'unsafe build lock: %s\n' "$lock" >&2; exit 2; }
# Caller owns this output namespace. Cooperating builds may create the same
# regular leaf; replacing its name concurrently is outside this contract.
# Check before opening so FIFOs cannot block and linked leaves are never opened.
if [ -L "$lock" ]; then refuse; fi
if [ -e "$lock" ]; then
    [ -f "$lock" ] || refuse
    [ "$(stat -c %h -- "$lock")" = 1 ] || refuse
fi
# Append creates a missing leaf without ever truncating an existing inode.
# Reuse the descriptor for flock and all descendants; never unlink the leaf.
exec 8>>"$lock"
[ ! -L "$lock" ] && [ -f "$lock" ] || refuse
[ "$(stat -Lc %h -- /proc/self/fd/8)" = 1 ] || refuse
[ "$(stat -Lc %d:%i -- /proc/self/fd/8)" = "$(stat -c %d:%i -- "$lock")" ] || refuse
flock -x 8
export OMEGA_BUILD_LOCKED_DIR=$canonical
# The make and its compiler children inherit fd 8. If make is terminated, an
# in-flight compiler keeps exclusion until it exits, rather than publishing late.
exec "$@"
