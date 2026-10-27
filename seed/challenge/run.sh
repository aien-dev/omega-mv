#!/bin/sh
# run.sh -- the seed-chain bootstrap challenge (seed/challenge/CHALLENGE.md).
# Rebuilds omc from the 408-byte seed0 (seed0 -> hex1 -> asm2 -> om4 -> omc -> omc -> omc) out of the
# sources committed at the pin named in expected.txt, and checks every input and every rung against the
# published sha256 and byte size there. Nothing in the working tree is written except the output dir.
#
#   sh seed/challenge/run.sh                 # from the repo root of a full clone (needs the pin commit)
#   sh seed/challenge/run.sh --tree DIR      # check an already extracted tree instead (negative controls)
#   options: --expect FILE (default seed/challenge/expected.txt), --out DIR (default build/seed/challenge)
#
# Exit 0 = CHALLENGE PASS, 1 = CHALLENGE FAIL, 2 = usage, 3 = NOT_RUN (not AArch64 Linux, or pin missing).
# The hash log is <out>/challenge.log.
#
# Host glue by design, like load0.sh and omega-from-seed.sh: it only runs the seed programs and compares
# with coreutils sha256sum, an outside oracle. It has no .om successor on purpose: a checker compiled by
# the chain it checks would be circular. The bytes of every rung are written by seed programs, not by C.
set -u
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
EXP=$R/seed/challenge/expected.txt
TREE=
OUT=$R/build/seed/challenge
while [ $# -gt 0 ]; do
  case $1 in
    --expect) [ $# -ge 2 ] || exit 2; EXP=$2; shift 2 ;;
    --tree)   [ $# -ge 2 ] || exit 2; TREE=$2; shift 2 ;;
    --out)    [ $# -ge 2 ] || exit 2; OUT=$2; shift 2 ;;
    *) echo "usage: run.sh [--expect FILE] [--tree DIR] [--out DIR]" >&2; exit 2 ;;
  esac
done

if [ "$(uname -s)/$(uname -m)" != Linux/aarch64 ]; then
  echo "CHALLENGE NOT_RUN: the seed programs are AArch64 Linux ELF files; this host is $(uname -s)/$(uname -m)"
  exit 3
fi
[ -r "$EXP" ] || { echo "CHALLENGE NOT_RUN: no expected table at $EXP"; exit 3; }
pin=$(awk '$1=="pin"{print $2}' "$EXP")
[ -n "$pin" ] || { echo "CHALLENGE NOT_RUN: expected table names no pin"; exit 3; }

rm -rf "$OUT"
mkdir -p "$OUT/b" || exit 3
B=$OUT/b
LOG=$OUT/challenge.log
: > "$LOG"
fails=0
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
ms() { date +%s%3N; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

if [ -z "$TREE" ]; then
  TREE=$OUT/src
  mkdir -p "$TREE"
  if ! git -C "$R" cat-file -e "$pin^{commit}" 2>/dev/null; then
    echo "CHALLENGE NOT_RUN: pin $pin is not in this clone (shallow?); run: git fetch origin $pin"
    exit 3
  fi
  git -C "$R" archive "$pin" seed/rung0 seed/rung1 seed/rung2 seed/rung3 seed/rung4 seed/omc \
    | tar -x -C "$TREE" || { echo "CHALLENGE NOT_RUN: git archive of $pin failed"; exit 3; }
  src="git archive $pin"
else
  TREE=$(CDPATH= cd -- "$TREE" && pwd) || exit 2
  src="--tree $TREE"
fi
S=$TREE/seed

say "omega seed-chain bootstrap challenge"
say "date $(date -u +%FT%TZ)"
say "host $(uname -srm)"
say "cpu $(awk -F': ' '/^(model name|CPU part)/{print $2; exit}' /proc/cpuinfo 2>/dev/null)"
say "checker_head $(git -C "$R" rev-parse HEAD 2>/dev/null || echo none)"
say "checker_sha256 $(sha "$0")"
say "expected_sha256 $(sha "$EXP")"
say "pin $pin"
say "source $src"

# check KIND NAME FILE MS: compare FILE with the table line "KIND NAME sha bytes".
check() {
  want=$(awk -v k="$1" -v n="$2" '$1==k && $2==n {print $3" "$4}' "$EXP")
  if [ -z "$want" ]; then say "FAIL $1 $2: no published value"; fails=$((fails+1)); return; fi
  if [ ! -f "$3" ]; then say "NOT_RUN $1 $2: not built"; fails=$((fails+1)); return; fi
  got="$(sha "$3") $(wc -c < "$3" | tr -d ' ')"
  if [ "$got" = "$want" ]; then say "PASS $1 $2 $got${4:+ ${4}ms}"
  else say "FAIL $1 $2 got $got want $want"; fails=$((fails+1)); fi
}
bad() { say "FAIL $*"; fails=$((fails+1)); }
# run OUTFILE INFILE PROG...: PROG < INFILE > OUTFILE; a nonzero status removes OUTFILE.
run() {
  o=$1; i=$2; shift 2
  if "$@" < "$i" > "$o" 2> "$o.err"; then chmod +x "$o"
  else bad "$(basename "$1") on $(basename "$i") exited $?: $(head -c 200 "$o.err")"; rm -f "$o"; fi
}
need() { [ -x "$1" ] || { say "NOT_RUN $2: $(basename "$1") missing"; fails=$((fails+1)); return 1; }; }

for f in $(awk '$1=="input"{print $2}' "$EXP"); do check input "$f" "$TREE/$f"; done

T0=$(ms)
# rung 0: seed0, the 408 hand-written bytes, loaded by POSIX printf (load0.sh), then rebuilding itself.
t=$(ms)
if sh "$S/rung0/load0.sh" "$S/rung0/seed0.hex" "$B/seed0" 2> "$B/load0.err"; then chmod +x "$B/seed0"
else bad "load0.sh: $(head -c 200 "$B/load0.err")"; rm -f "$B/seed0"; fi
check rung seed0 "$B/seed0" $(( $(ms) - t ))
if need "$B/seed0" seed0.self; then
  run "$B/seed0.self" "$S/rung0/seed0.hex" "$B/seed0"
  cmp -s "$B/seed0" "$B/seed0.self" && say "PASS seed0 rebuilds itself" || bad "seed0 did not rebuild itself"
fi
# rung 1: hex1, written by seed0, then rebuilding itself.
t=$(ms)
need "$B/seed0" hex1 && run "$B/hex1" "$S/rung1/hex1.hex" "$B/seed0"
check rung hex1 "$B/hex1" $(( $(ms) - t ))
if need "$B/hex1" hex1.self; then
  run "$B/hex1.self" "$S/rung1/hex1.hex" "$B/hex1"
  cmp -s "$B/hex1" "$B/hex1.self" && say "PASS hex1 rebuilds itself" || bad "hex1 did not rebuild itself"
fi
# rung 2: asm2, written by hex1.
t=$(ms)
need "$B/hex1" asm2 && run "$B/asm2" "$S/rung2/asm2.hex1" "$B/hex1"
check rung asm2 "$B/asm2" $(( $(ms) - t ))
# rung 4: om4, written by asm2 (as seed/rung4/mk4.sh does, on a copy: the p_filesz line of om4.s must
# already carry the size of the image it describes).
t=$(ms)
if need "$B/asm2" om4; then
  cat "$S/rung2/table.s2" "$S/rung3/table3.s2" "$S/rung4/table4.s2" "$S/rung4/om4.s" > "$B/om4.in"
  run "$B/om4.first" "$B/om4.in" "$B/asm2"
  if [ -f "$B/om4.first" ]; then
    n=$(wc -c < "$B/om4.first" | tr -d ' ')
    sed "s/^  \.w 0x[0-9a-f]* ; p_filesz = the size of om4.bin.*/  .w $(printf 0x%08x "$n") ; p_filesz = the size of om4.bin, $n bytes (asm2 has no size arithmetic; gate4.sh checks it)/" \
      "$S/rung4/om4.s" > "$B/om4.s.sized"
    cmp -s "$S/rung4/om4.s" "$B/om4.s.sized" && say "PASS om4.s p_filesz already $n" || bad "om4.s p_filesz is not $n"
    cat "$S/rung2/table.s2" "$S/rung3/table3.s2" "$S/rung4/table4.s2" "$B/om4.s.sized" > "$B/om4.in2"
    run "$B/om4" "$B/om4.in2" "$B/asm2"
  fi
fi
check rung om4 "$B/om4" $(( $(ms) - t ))
# rung 5: omc, written by om4 (stage1), then by itself twice (stage2, stage3). All three must be equal.
for st in 1 2 3; do
  case $st in 1) by=$B/om4 ;; 2) by=$B/stage1 ;; 3) by=$B/stage2 ;; esac
  t=$(ms)
  need "$by" "omc stage$st" && run "$B/stage$st" "$S/omc/omc.om" "$by"
  d=$(( $(ms) - t ))
  if [ -f "$B/stage$st" ]; then
    cp "$B/stage$st" "$B/omc"
    printf 'stage%s ' "$st" >> "$LOG"; printf 'stage%s ' "$st"
    check rung omc "$B/omc" "$d"
  else say "NOT_RUN rung omc stage$st"; fi
done
# The seed-born compiler compiles and runs a program (seed/rung4/tests/r5b_fwd.om returns 7).
if need "$B/stage3" probe; then
  run "$B/fwd" "$S/rung4/tests/r5b_fwd.om" "$B/stage3"
  if [ -x "$B/fwd" ]; then
    "$B/fwd" > /dev/null; rc=$?
    [ "$rc" = 7 ] && say "PASS stage3 compiles r5b_fwd.om; it runs and exits 7" || bad "r5b_fwd ran as $rc, wanted 7"
  fi
fi
say "chain_ms $(( $(ms) - T0 ))"
if [ "$fails" = 0 ]; then say "CHALLENGE PASS pin=$pin omc=$(sha "$B/stage3")"; exit 0; fi
say "CHALLENGE FAIL fails=$fails pin=$pin"
exit 1
