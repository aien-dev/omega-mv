#!/bin/bash
# test_clean_clone.sh -- tests of the clean-clone reproduction shared by the M18
# and M19 CLEAN_CLONE gates (src/omega_clean_clone.c). Host only (no GPU): the
# nested run is the host-only M6 suite. Needs PHYSICS_DIR at the physics.lock pin
# (absolute). Run: make test-clean-clone PHYSICS_DIR=<pinned physics checkout>
#
# Works on a scratch clone of HEAD so the caller's tree may be dirty:
#   1. OUT_DIR exported the way tests/gates/run.sh does (omega #511) still builds
#      and runs the clone (the root cause of the 2026-10-19 OMEGA-M19 failures);
#   2. a dirty source tree is refused before anything is cloned;
#   3. a nested gate run that fails is reported with its step and log tail;
#   4. a candidate commit that does not build is reported at the make step with
#      the compiler error in the tail.
set -u
HERE=$(cd -P "$(dirname "$0")/.." && pwd)
: "${PHYSICS_DIR:?set PHYSICS_DIR to the pinned physics checkout}"
PHYSICS_DIR=$(cd "$PHYSICS_DIR" && pwd) || exit 1
export PHYSICS_DIR
TMP=$(mktemp -d /tmp/omega-clean-clone-test.XXXXXX)
trap 'rm -rf "$TMP" /tmp/omega_clean_selftest_*' EXIT
fails=0
ok() { echo "  [PASS] $1"; }
bad() { echo "  [FAIL] $1"; fails=$((fails + 1)); }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }

src=$TMP/src
git clone --quiet --no-hardlinks "$HERE" "$src" || exit 1
git -C "$src" checkout --quiet --detach "$(git -C "$HERE" rev-parse HEAD)" || exit 1
sha=$(git -C "$src" rev-parse HEAD)
drv=$TMP/driver
cc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -I"$src/src" -I"$PHYSICS_DIR/nvrm" \
    -I"$PHYSICS_DIR/m16" -o "$drv" "$src/tests/test_clean_clone_driver.c" "$src/src/omega_clean_clone.c" \
    "$src/src/omega_evidence.c" "$src/src/sha256.c" || { echo "FAIL: cannot build driver"; exit 1; }
run() { (cd "$src" && "$drv" "$@" >"$TMP/out" 2>"$TMP/err"); }

echo "1. OUT_DIR exported like tests/gates/run.sh"
check "clone built and nested M6 run passed" '(export OUT_DIR=build/step-chip-test-m19; run --run-m6-gates)'
check "  bound to the exact candidate" 'grep -qx "CLEAN_CLONE_OK $sha" "$TMP/out"'
check "  clone directory removed on success" '! ls -d /tmp/omega_clean_selftest_* >/dev/null 2>&1'

echo "2. dirty source tree"
touch "$src/untracked-mutant"
check "dirty tree refused" '! run --run-m6-gates'
check "  says why" 'grep -q "step source_clean failed: source tree at $sha is dirty" "$TMP/err"'
check "  nothing cloned" '! ls -d /tmp/omega_clean_selftest_* >/dev/null 2>&1'
rm -f "$src/untracked-mutant"

echo "3. nested gate run fails"
check "failing nested run refused" '! run --no-such-gates'
check "  names the step and exit status" 'grep -q "step 5-run-gates failed: exit status 1" "$TMP/err"'
check "  prints the log tail" 'grep -q "last lines of /tmp/omega_clean_selftest_.*/5-run-gates.log" "$TMP/err"'
rm -rf /tmp/omega_clean_selftest_*

echo "4. candidate that does not build"
printf '#error clean-clone-mutant\n' >> "$src/src/omega_vector.c"
git -C "$src" -c user.name=Test -c user.email=test@example.invalid commit -qam "mutant: does not build"
bsha=$(git -C "$src" rev-parse HEAD)
check "broken candidate refused" '! run --run-m6-gates'
check "  names the make step" 'grep -q "step 4-make failed: exit status" "$TMP/err"'
check "  tail shows the compiler error" 'grep -q "clean-clone-mutant" "$TMP/err"'
check "  candidate was the new commit" '[ "$bsha" != "$sha" ]'
rm -rf /tmp/omega_clean_selftest_*

if [ "$fails" -ne 0 ]; then echo "test-clean-clone: FAIL ($fails)"; exit 1; fi
echo "test-clean-clone: PASS"
