#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# aflpp.sh DEST: build the pinned AFL++ used by run.sh (host tooling, not part of the library).
# AFL++ v4.40c at e5a8ba39ecf97d05e286fdd4e01da96554dbf64f + aflpp-exitrange.patch (AFL_CRASH_EXITCODE=LO-HI,
# so every Omega trap exit 101..114 is a crash), qemu_mode for aarch64 built inside the pinned
# ubuntu:24.04 image because the host lacks glib headers. frida_mode cannot load static omc binaries.
set -eu
DEST=${1:?usage: aflpp.sh DEST}
D=$(cd -P "$(dirname "$0")" && pwd -P)
IMG=ubuntu@sha256:68ee22004a9109b44973313b02ff7b1de250f1de0f5ecb628d73f579aaff33ac
git clone --branch v4.40c https://github.com/AFLplusplus/AFLplusplus.git "$DEST"
cd "$DEST"
[ "$(git rev-parse HEAD)" = e5a8ba39ecf97d05e286fdd4e01da96554dbf64f ] || { echo "AFL++ tag moved" >&2; exit 1; }
git apply "$D/aflpp-exitrange.patch"
nice -n 19 ionice -c3 make -j2 afl-fuzz afl-showmap afl-tmin
docker run --rm -v "$DEST":/aflpp -v "$D/build-qemu-docker.sh":/b.sh:ro "$IMG" sh /b.sh
ls -l afl-qemu-trace
