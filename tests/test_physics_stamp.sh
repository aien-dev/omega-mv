#!/bin/bash
# tests/test_physics_stamp.sh PHYSICS_DIR
# omega-mv#580 root cause guard. The roll/cmdlist chip test failed all 5 checks on 2026-10-19 because its
# build dir held nvrm.o and m16_native.o compiled from ../physics (a checkout without physics 6d7cf0d4, the
# GB10 GPU-uncached GPFIFO/USERD fix). A later `make PHYSICS_DIR=<pinned>` kept those objects: make compares
# mtimes, and the pinned sources were older than the stale objects. The GPU never saw the new GPFIFO entries,
# the first marker wait timed out and latched the session.
# This test switches PHYSICS_DIR between two trees whose nvrm.c differ but whose new source is OLDER than the
# object already built, and requires the object to be rebuilt from the new tree.
# Host only, no GPU. Verdict line last: PHYSICS_STAMP_TEST: PASS|FAIL
set -u
P1="${1:?pinned physics dir}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
T="$(mktemp -d "${TMPDIR:-/tmp}/physstamp.XXXXXX")"
trap 'rm -rf "$T"' EXIT
P2="$T/physics-variant"
mkdir -p "$P2"
for e in "$P1"/* "$P1"/.git; do [ -e "$e" ] || continue; b=$(basename "$e"); [ "$b" = nvrm ] || ln -s "$e" "$P2/$b"; done
cp -r "$P1/nvrm" "$P2/nvrm"
echo 'int omega_physics_stamp_probe_variant;' >> "$P2/nvrm/nvrm.c"
touch -d '2000-01-01 00:00:00' "$P2/nvrm/nvrm.c"
OUT="$T/out"
fail=0
cd "$HERE" || exit 2
make -s PHYSICS_DIR="$P1" OUT_DIR="$OUT" "$OUT/nvrm.o" >"$T/b1.log" 2>&1 || { echo "build with P1 failed"; cat "$T/b1.log"; fail=1; }
if nm "$OUT/nvrm.o" 2>/dev/null | grep -q omega_physics_stamp_probe_variant; then echo "FAIL: P1 object already has the variant symbol"; fail=1; else echo "ok: object built from the pinned physics"; fi
make -s PHYSICS_DIR="$P2" OUT_DIR="$OUT" "$OUT/nvrm.o" >"$T/b2.log" 2>&1 || { echo "build with P2 failed"; cat "$T/b2.log"; fail=1; }
if nm "$OUT/nvrm.o" 2>/dev/null | grep -q omega_physics_stamp_probe_variant; then echo "ok: PHYSICS_DIR switch rebuilt nvrm.o from the new tree"; else echo "FAIL: PHYSICS_DIR switch kept the stale nvrm.o (the omega-mv#580 failure)"; fail=1; fi
make -s PHYSICS_DIR="$P1" OUT_DIR="$OUT" "$OUT/nvrm.o" >"$T/b3.log" 2>&1 || fail=1
if nm "$OUT/nvrm.o" 2>/dev/null | grep -q omega_physics_stamp_probe_variant; then echo "FAIL: switching back kept the variant object"; fail=1; else echo "ok: switching back rebuilt from the pinned physics"; fi
make -s PHYSICS_DIR="$P1" OUT_DIR="$OUT" "$OUT/omega_gpu_session.o" >"$T/b4.log" 2>&1 || fail=1
before=$(stat -c %Y.%N "$OUT/omega_gpu_session.o" 2>/dev/null)
sleep 1
make -s PHYSICS_DIR="$P1" OUT_DIR="$OUT" "$OUT/omega_gpu_session.o" "$OUT/nvrm.o" >"$T/b5.log" 2>&1 || fail=1
after=$(stat -c %Y.%N "$OUT/omega_gpu_session.o" 2>/dev/null)
if [ "$before" = "$after" ]; then echo "ok: same physics, nothing rebuilt"; else echo "FAIL: same physics rebuilt objects"; fail=1; fi
if [ $fail = 0 ]; then echo "PHYSICS_STAMP_TEST: PASS"; else echo "PHYSICS_STAMP_TEST: FAIL"; exit 1; fi
