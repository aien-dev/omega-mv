#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# The canonical archive must be built only from the authenticated CUDApp tree. A parent
# make passes PHYSICS_DIR through MAKEFLAGS; exported PHYSICS_DIR/CFLAGS/CUDAPP_ENCODER
# must not substitute another driver client, flags or encoder either.
# Usage: tests/cudapp_stage_hermetic.sh CUDAPP_CHECKOUT   (needs OMEGA_OMC, OMEGA_BRIDGE)
set -eu
checkout=${1:?usage: tests/cudapp_stage_hermetic.sh CUDAPP_CHECKOUT}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/cudapp-stage-hermetic.XXXXXX")
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
cd "$root"
env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL -u PHYSICS_DIR -u CFLAGS -u CUDAPP_ENCODER \
    sh tools/cudapp-stage.sh cudapp.lock "$checkout" "$work/clean" > /dev/null
# Hostile driver client: the authenticated nvrm/m16 plus a marker symbol.
mkdir "$work/hostile"
cp -R "$work/clean/source/nvrm" "$work/clean/source/m16" "$work/clean/source/third_party" "$work/hostile/"
printf '\nconst char omega_stage_hostile_marker[] = "OMEGA_STAGE_HOSTILE_MARKER";\n' >> "$work/hostile/nvrm/nvrm.c"
clean=$(sha256sum < "$work/clean/build/libcudapp.a" | cut -c1-64)
fail() { echo "FAIL CUDAPP_STAGE_HERMETIC $1" >&2; exit 1; }
# Case 1: a parent make's PHYSICS_DIR (MAKEFLAGS and environment) naming the hostile client.
MAKEFLAGS=" -- PHYSICS_DIR=$work/hostile" PHYSICS_DIR="$work/hostile" \
    sh tools/cudapp-stage.sh cudapp.lock "$checkout" "$work/physics" > /dev/null 2>&1 ||
    fail "physics: stage failed under inherited PHYSICS_DIR"
grep -q OMEGA_STAGE_HOSTILE_MARKER "$work/physics/build/libcudapp.a" &&
    fail "physics: inherited PHYSICS_DIR driver client entered the archive"
[ "$(sha256sum < "$work/physics/build/libcudapp.a" | cut -c1-64)" = "$clean" ] ||
    fail "physics: archive depends on inherited PHYSICS_DIR"
# Case 2: exported compiler flags and encoder selection.
CFLAGS=-O0 CUDAPP_ENCODER=c \
    sh tools/cudapp-stage.sh cudapp.lock "$checkout" "$work/flags" > /dev/null 2>&1 ||
    fail "flags: stage failed under exported CFLAGS/CUDAPP_ENCODER"
[ "$(sha256sum < "$work/flags/build/libcudapp.a" | cut -c1-64)" = "$clean" ] ||
    fail "flags: archive depends on exported CFLAGS/CUDAPP_ENCODER"
echo "PASS CUDAPP_STAGE_HERMETIC archive=$clean"
