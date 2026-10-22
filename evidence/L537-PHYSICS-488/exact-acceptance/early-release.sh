#!/bin/sh
# Keep this inode adjacent to OUT_DIR: `make clean` removes OUT_DIR itself.
# Never unlink the lock file, even after release: waiters may hold its inode.
set -eu
out=${1:?output directory required}
shift
mkdir -p -- "$out"
canonical=$(cd -- "$out" && pwd -P)
exec 8>"$canonical.omega-build.lock"
flock -x 8
flock -u 8
export OMEGA_BUILD_LOCKED_DIR=$canonical
# The make and its compiler children inherit fd 8. If make is terminated, an
# in-flight compiler keeps exclusion until it exits, rather than publishing late.
exec "$@"
