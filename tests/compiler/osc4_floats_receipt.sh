#!/bin/sh
# OSC-4 float slice receipt (docs/osc/OSC-4-FLOATS.md), gates G1..G4.
# Usage: tests/compiler/osc4_floats_receipt.sh   (from a clean tree; nice, one make)
#  1. refuses a dirty tree: receipts record an exact commit
#  2. runs `make test-compiler-full` into a temporary OUT_DIR: G3 (every existing compiler
#     suite, full counts) plus G1 (every slice operation, OSC_FLOAT_G1_N operand triples,
#     native and interpreter against the C reference) and G2 (dot products and softmax)
#  3. G4: builds oscc at the main merge base and at the Slice A merge (OSC4_SLICEA_COMMIT)
#     in temporary worktrees and runs tests/compiler/float_g4.sh against each
#  4. records the box load (/proc/loadavg) before and after, and the G1 wall time
#  5. writes evidence/OSC-4/receipts/osc4-floats-<sha256>.json; never overwrites evidence
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd -P)
cd "$ROOT"
EVD=evidence/OSC-4/receipts
SLICEA=${OSC4_SLICEA_COMMIT:-b94722f4}
die() { echo "osc4-floats-receipt: REFUSED: $*" >&2; exit 2; }
[ -z "$(git status --porcelain --untracked-files=all)" ] || die "dirty tree; commit first (receipts record an exact commit)"
COMMIT=$(git rev-parse HEAD)
BASE=$(git merge-base HEAD origin/main) || die "no merge base with origin/main"
SLICEA_FULL=$(git rev-parse "$SLICEA^{commit}") || die "no Slice A commit $SLICEA"
LOAD0=$(cat /proc/loadavg)

T=$(mktemp -d)
cleanup() {
    for w in "$T/wt-main" "$T/wt-slicea"; do [ -d "$w" ] && git worktree remove --force "$w" 2>/dev/null || true; done
    rm -rf "$T"
}
trap cleanup EXIT
O="$T/build/compiler"
g0=$(date +%s)
nice make --no-print-directory OUT_DIR="$T/build" PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 test-compiler-full \
    >"$T/make.log" 2>&1 || { tail -30 "$T/make.log"; die "make test-compiler-full failed"; }
g1=$(date +%s)
grep -q '^test-compiler: PASS' "$T/make.log" || die "no test-compiler PASS line (G3)"
grep -q '^test-compiler-float-g1: PASS' "$T/make.log" || die "no G1 PASS line"
grep -q '^test-compiler-float-g2: PASS' "$T/make.log" || die "no G2 PASS line"

# G4 against main and against the Slice A merge
G4=""
for pair in "main:$BASE" "slicea:$SLICEA_FULL"; do
    name=${pair%%:*}; rev=${pair#*:}
    w="$T/wt-$name"
    git worktree add --detach "$w" "$rev" >/dev/null 2>&1 || die "cannot check out $rev"
    nice make --no-print-directory -C "$w" OUT_DIR="$w/build" PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 "$w/build/compiler/oscc" \
        >"$T/oscc-$name.log" 2>&1 || { tail -20 "$T/oscc-$name.log"; die "cannot build oscc at $rev"; }
    sh tests/compiler/float_g4.sh "$w/build/compiler/oscc" "$O/oscc" tests/compiler/progs "$T/g4-$name" >"$T/g4-$name.out" 2>&1 \
        || { cat "$T/g4-$name.out"; die "G4 against $name ($rev) failed"; }
    G4="$G4    {\"against\": \"$name\", \"commit\": \"$rev\", \"result\": \"$(tail -1 "$T/g4-$name.out")\"},
"
done
LOAD1=$(cat /proc/loadavg)

esc() { sed 's/\\/\\\\/g; s/"/\\"/g'; }
lines() { esc | sed 's/.*/    "&",/' | sed '$ s/,$//'; }
R="$T/receipt.json"
{
  printf '{\n'
  printf '  "schema": "omega.osc4.floats.receipt.v1",\n'
  printf '  "verdict": "PASS",\n'
  printf '  "statement": "OSC-4 float slice (f16, bf16, f8e4m3, fma, strict) on the CPU; no vectors, no GPU float ops; not a general Omega compiler; no self-hosting.",\n'
  printf '  "design": "docs/osc/OSC-4-FLOATS.md",\n'
  printf '  "repo_commit": "%s",\n' "$COMMIT"
  printf '  "base_commit": "%s",\n' "$BASE"
  printf '  "slice_a_commit": "%s",\n' "$SLICEA_FULL"
  printf '  "tree_clean": true,\n'
  printf '  "host": "%s %s",\n' "$(uname -m)" "$(uname -r)"
  printf '  "cpu": "%s",\n' "$(grep -m1 -i 'cpu part' /proc/cpuinfo | esc) / features: $(grep -m1 Features /proc/cpuinfo | grep -o 'fphp\|bf16\|afp' | paste -sd' ' -)"
  printf '  "c_compiler": "%s",\n' "$(${CC:-cc} --version 2>/dev/null | head -1 | esc)"
  printf '  "box_load_before": "%s",\n' "$LOAD0"
  printf '  "box_load_after": "%s",\n' "$LOAD1"
  printf '  "test_compiler_full_wall_seconds": %s,\n' "$((g1 - g0))"
  printf '  "g3_test_compiler": [\n'
  { grep -h '^golden=' "$O/compiler.out" | sed 's/ time=.*//; s/^/plain: /'; grep -h '^golden=' "$O/compiler_asan.out" | sed 's/ time=.*//; s/^/asan: /';
    grep -h '^float fuzz:\|^float slice fuzz:' "$O/compiler.out" | sed 's/^/plain: /'; grep -h '^float slice fuzz:' "$O/compiler_asan.out" | sed 's/^/asan: /';
    grep -h '^test-compiler: PASS' "$T/make.log"; } | lines
  printf '  ],\n'
  printf '  "g1": [\n'
  grep -h '^g1' "$O/float_g1.out" | lines
  printf '  ],\n'
  printf '  "g1_asan": "%s",\n' "$(grep -h '^g1: cases=' "$O/float_g1_asan.out" | esc)"
  printf '  "g2": [\n'
  grep -h '^g2' "$O/float_g2.out" | lines
  printf '  ],\n'
  printf '  "g4": [\n'
  printf '%s' "$G4" | sed '$ s/},$/}/'
  printf '  ],\n'
  printf '  "timing_measured": "wall time of the whole test run only; no speed claim"\n'
  printf '}\n'
} >"$R"
mkdir -p "$EVD"
RSHA=$(sha256sum "$R" | cut -d' ' -f1)
OUT="$EVD/osc4-floats-$RSHA.json"
[ -e "$OUT" ] && { echo "osc4-floats-receipt: identical receipt already present: $OUT"; exit 0; }
cp "$R" "$OUT"
echo "osc4-floats-receipt: PASS receipt=$OUT"
