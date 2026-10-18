#!/bin/bash
# usage: tools/check_physics_stamp.sh <omega-worktree> <scratch-out-dir> <pinned-physics> <other-physics-commit>; exit 0 when nvrm.o comes from the pinned physics after a failed-then-fixed build
W=$1; O=$2; GOOD=$3; BAD=$4
cd "$W" || exit 2; rm -rf "$O"
make -j16 OUT_DIR="$O" PHYSICS_DIR="$BAD" "$O/libomega_gpu.a" >/dev/null 2>&1; echo "first build (wrong physics) rc=$?"
ls "$O"/nvrm.o >/dev/null 2>&1 && echo "after first build nvrm.o exists from: $(grep -ho '[^ ]*/nvrm/nvrm\.c' $O/nvrm.d)" || echo "after first build: no nvrm.o"
make -j16 OUT_DIR="$O" PHYSICS_DIR="$GOOD" "$O/libomega_gpu.a" >/dev/null 2>&1; echo "second build (pinned physics) rc=$?"
src=$(grep -ho '[^ ]*/nvrm/nvrm\.c' $O/nvrm.d)
echo "archive nvrm.o built from: $src"
case "$src" in "$GOOD"/*) echo PHYS-STAMP PASS; exit 0;; *) echo PHYS-STAMP FAIL; exit 1;; esac
