#!/usr/bin/env bash
# tools/c2om/equiv/equiv.sh <artifact.conf>: the equivalence harness of the C-to-Omega program (ledger L418).
#
# For one artifact:
#   1. the C oracle is built by gcc (through quiet-build make) from a PINNED commit, in a scratch tree;
#   2. the Omega replacement is built ONLY by the seed-born omc (tools/c2om/seed-omc.sh, built from
#      seed0.hex); its sha256 goes into the evidence;
#   3. every case runs on both, each in its own empty directory: stdout, stderr, exit code and every
#      file the case names must be the same bytes;
#   4. second oracle: the spec-only interpreter tools/omega_ref (origin/seed/omega-ref, pinned) runs the
#      same Omega source; its stdout and exit code must equal the omc build's;
#   5. negative control: the conf's one-word MUTANT of the Omega source must still compile and must make
#      at least one case differ (else the harness is blind and the gate fails).
# Exit 0 only when cases run = cases written, every case is identical on all three, and the mutant is caught.
#
# The conf is a shell file setting:
#   NAME        artifact name (evidence goes to evidence/C2OM/$NAME/)
#   C_COMMIT    commit holding the C sources (a sha or a tag such as c-oracle/<name>)
#   C_FILES     paths (at C_COMMIT) to extract
#   C_BUILD     make recipe line(s) run in the extracted tree; must write $(OUT)/c.bin
#   OM_SRC      the Omega source in the live tree
#   CASES       case file: one case per line, "<id><TAB><stdin file or -><TAB><args>"; '#' lines skipped
#   OUT_FILES   files a case writes in its directory (compared too), may be empty
#   MUTANT      a sed expression changing one word of OM_SRC
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
[ $# -eq 1 ] || { echo "usage: $0 <artifact.conf>" >&2; exit 2; }
CONF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
OUT_FILES=; REF_COMMIT=${REF_COMMIT:-origin/seed/omega-ref}
# shellcheck disable=SC1090
. "$CONF"
cd "$ROOT"
EV=$ROOT/evidence/C2OM/$NAME
B=$ROOT/build/c2om/equiv/$NAME
mkdir -p "$EV" "$B"
LOG=$EV/equiv.log
: > "$LOG"
say() { echo "$*" | tee -a "$LOG"; }
fail() { say "FAIL $*"; say "RESULT FAIL"; exit 1; }

# 1. omc from the seed
OMC_DIR=$ROOT/build/c2om/seed
tools/c2om/seed-omc.sh "$OMC_DIR" > "$B/seed-omc.out" 2>&1 || fail "seed omc build: $(tail -1 "$B/seed-omc.out")"
OMC=$OMC_DIR/omc; OMC_SHA=$(cat "$OMC_DIR/omc.sha256")
say "artifact     $NAME"
say "omc sha256   $OMC_SHA (built from seed/rung0/seed0.hex by tools/c2om/seed-omc.sh; fixpoint checked)"

# 2. C oracle from the pinned commit, built by gcc through quiet-build make
csha=$(git rev-parse "$C_COMMIT^{commit}")
rm -rf "$B/csrc"; mkdir -p "$B/csrc"
# shellcheck disable=SC2086
git archive "$csha" $C_FILES | tar -x -C "$B/csrc"
printf 'OUT := %s\nCC ?= gcc\nall:\n%s\n' "$B" "$(printf '%s\n' "$C_BUILD" | sed 's/^/\t/')" > "$B/csrc/oracle.mk"
rm -f "$B/c.bin"
quiet-build make -s -C "$B/csrc" -f oracle.mk > "$B/cbuild.out" 2>&1 || fail "C oracle build: $(tail -3 "$B/cbuild.out")"
[ -x "$B/c.bin" ] || fail "C oracle build wrote no c.bin"
say "C oracle     $C_COMMIT = $csha ($C_FILES), $(gcc --version | head -1)"

# 3. second oracle: the spec-only interpreter, pinned
rsha=$(git rev-parse "$REF_COMMIT^{commit}")
if [ ! -x "$B/../omega_ref-$rsha/omega_ref" ]; then
  rm -rf "$B/../omega_ref-$rsha.src"; mkdir -p "$B/../omega_ref-$rsha.src" "$B/../omega_ref-$rsha"
  git archive "$rsha" tools/omega_ref | tar -x -C "$B/../omega_ref-$rsha.src"
  quiet-build gcc -std=gnu11 -O2 -Wall -o "$B/../omega_ref-$rsha/omega_ref" "$B/../omega_ref-$rsha.src/tools/omega_ref/omega_ref.c" \
    > "$B/refbuild.out" 2>&1 || fail "omega_ref build: $(tail -3 "$B/refbuild.out")"
fi
REF=$B/../omega_ref-$rsha/omega_ref
say "interpreter  tools/omega_ref at $rsha"

# 4. the Omega build, by omc only
t0=$(date +%s%N)
"$OMC" < "$OM_SRC" > "$B/om.bin" 2> "$B/om.err" || fail "omc refused $OM_SRC: $(cat "$B/om.err")"
t1=$(date +%s%N)
chmod +x "$B/om.bin"
say "Omega        $OM_SRC sha256 $(sha256sum "$OM_SRC" | cut -c1-16) -> $(wc -c < "$B/om.bin") bytes in $(( (t1 - t0) / 1000 )) us"

# one run: <bin-or-'ref'> <dir> <stdin> <args...>
run1() {
  local bin=$1 d=$2 in=$3; shift 3
  rm -rf "$d"; mkdir -p "$d"
  [ "$in" = - ] && in=/dev/null
  set +e
  if [ "$bin" = ref ]; then (cd "$d" && timeout 120 "$REF" run "$ROOT/$OM_SRC" "$@" < "$in" > ../$(basename "$d").out 2> ../$(basename "$d").err)
  else (cd "$d" && timeout 120 "$bin" "$@" < "$in" > ../$(basename "$d").out 2> ../$(basename "$d").err); fi
  echo $? > "$d.rc"
  set -e
}
same_run() { # a b [stderr too]
  cmp -s "$1.out" "$2.out" && cmp -s "$1.rc" "$2.rc" || return 1
  [ "${3:-1}" = 0 ] || cmp -s "$1.err" "$2.err" || return 1
  local f; for f in $OUT_FILES; do
    [ -e "$1/$f" ] || [ -e "$2/$f" ] || continue
    cmp -s "$1/$f" "$2/$f" || return 1
  done
}

# 5. cases
written=$(grep -cvE '^\s*(#|$)' "$CASES")
run=0; pass=0
TSV=$EV/cases.tsv
printf 'case\tC rc\tomc rc\tinterp rc\tstdout bytes\tC = omc\tomc = interp\n' > "$TSV"
while IFS=$'\t' read -r id in args; do
  case $id in ''|\#*) continue;; esac
  [ "$in" = - ] || in=$ROOT/$in
  # shellcheck disable=SC2086
  run1 "$B/c.bin" "$B/run/c" "$in" $args
  # shellcheck disable=SC2086
  run1 "$B/om.bin" "$B/run/o" "$in" $args
  # shellcheck disable=SC2086
  run1 ref "$B/run/r" "$in" $args
  run=$((run+1))
  co=no; or=no
  same_run "$B/run/c" "$B/run/o" && co=yes
  same_run "$B/run/o" "$B/run/r" 0 && or=yes
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$id" "$(cat "$B/run/c.rc")" "$(cat "$B/run/o.rc")" "$(cat "$B/run/r.rc")" \
    "$(wc -c < "$B/run/o.out")" "$co" "$or" >> "$TSV"
  if [ $co = yes ] && [ $or = yes ]; then pass=$((pass+1))
  else say "DIFF case $id: C = omc $co, omc = interpreter $or"; cmp "$B/run/c.out" "$B/run/o.out" 2>&1 | head -2 | tee -a "$LOG"; fi
done < "$CASES"
say "cases        written $written, run $run, identical on C, omc and interpreter $pass"

# 6. negative control
sed "$MUTANT" "$OM_SRC" > "$B/mutant.om"
cmp -s "$B/mutant.om" "$OM_SRC" && fail "negative control: MUTANT changed nothing"
[ "$(diff "$OM_SRC" "$B/mutant.om" | grep -c '^[<>]')" = 2 ] || fail "negative control: MUTANT must change exactly one line"
"$OMC" < "$B/mutant.om" > "$B/mutant.bin" 2> "$B/mutant.err" || fail "negative control: the mutant does not compile ($(cat "$B/mutant.err")); it must compile and change behaviour"
chmod +x "$B/mutant.bin"
caught=0
while IFS=$'\t' read -r id in args; do
  case $id in ''|\#*) continue;; esac
  [ "$in" = - ] || in=$ROOT/$in
  # shellcheck disable=SC2086
  run1 "$B/c.bin" "$B/run/c" "$in" $args
  # shellcheck disable=SC2086
  run1 "$B/mutant.bin" "$B/run/m" "$in" $args
  same_run "$B/run/c" "$B/run/m" || caught=$((caught+1))
done < "$CASES"
say "control      one-word mutant ($MUTANT) compiles; cases that catch it: $caught"
[ $caught -gt 0 ] || fail "negative control: the mutant passed every case (harness blind)"
[ $run = "$written" ] || fail "cases run $run != written $written"
[ $pass = "$run" ] || fail "$((run-pass)) of $run cases differ"
say "RESULT PASS"
