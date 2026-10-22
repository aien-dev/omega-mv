#!/bin/sh
# gate_ref.sh -- the reference interpreter (tools/omega_ref, an oracle outside the seed chain) against the
# om4-made programs on every gate4 case. Run from the repo root:  sh tools/omega_ref/gate_ref.sh
# Needs build/omegese/omegese and build/omega_emit/omega_emit (only to make the .oscir that the referee
# runner reads parameter types from), cc. Reads seed/ only. Results: evidence/OMEGA-REF/gate_ref.log.
set -eu
R=seed/rung4; X=$R/tests; B=build/omega_ref/gate; E=evidence/OMEGA-REF
T=tests/one_layer/exp445/dev/tasks/cases; O=tests/one_layer/omegese/dev
mkdir -p "$B" "$E"
fail=0; written=0; run=0
LOG=$E/gate_ref.log
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
bad() { say "FAIL: $*"; fail=1; }
: > "$LOG"
say "gate_ref $(date -u +%FT%TZ) at $(git rev-parse HEAD), om4.bin $(sha256sum $R/om4.bin | cut -c1-16)"
sh tools/omega_ref/build.sh
I=build/omega_ref/omega_ref
[ -x build/omegese/omegese ] && [ -x build/omega_emit/omega_emit ] || { bad "C path tools not built"; exit 1; }
quiet-build cc -O1 -Isrc -Isrc/compiler -o $B/runner seed/check/runner.c \
  $(ls src/compiler/*.c | grep -v oscc_main.c) src/sha256.c

# 1. function mode: the 10 dev tasks and fm1-9. om4's words run by the referee runner; the interpreter
#    calls the entry itself. Every input line is a case: value and raw x0, or the trap number.
fmode() { # name entry inputs source  [interpreter]
  interp=${5:-$I}
  build/omegese/omegese emit $4 $B/$1.json > /dev/null
  build/omega_emit/omega_emit emit $B/$1.json $B/$1.oscir > /dev/null
  $R/om4.bin < $4 > $B/$1.om4.bin
  $B/runner $B/$1.oscir "$2" $3 $B/$1.om4.bin | tail -n +2 > $B/$1.om4.res
  $interp call $4 "$2" $3 > $B/$1.ref.res
  n=$(wc -l < $3)
  cmp -s $B/$1.om4.res $B/$1.ref.res
}
for d in dev01 dev02 dev03 dev04 dev05 dev06 dev07 dev08 dev09 dev10; do
  entry=$(awk '$1=="entry"{print $2}' $T/$d/requirement)
  n=$(wc -l < $T/$d/hidden/inputs); written=$((written + n))
  if fmode $d "$entry" $T/$d/hidden/inputs $O/$d.omegese; then run=$((run + n)); say "PASS $d: $n inputs, interpreter = om4 ($(grep -c TRAP $B/$d.ref.res || true) traps)"
  else run=$((run + n)); bad "$d: interpreter differs from om4"; diff $B/$d.om4.res $B/$d.ref.res | head -5 | tee -a "$LOG"; fi
done
for f in fm1 fm2 fm3 fm4 fm5 fm6 fm7 fm8 fm9; do
  n=$(wc -l < $X/$f.inputs); written=$((written + n))
  if fmode $f "$(cat $X/$f.entry)" $X/$f.inputs $X/$f.omegese; then run=$((run + n)); say "PASS $f: $n inputs, interpreter = om4 ($(grep -c TRAP $B/$f.ref.res || true) traps)"
  else run=$((run + n)); bad "$f: interpreter differs from om4"; diff $B/$f.om4.res $B/$f.ref.res | head -5 | tee -a "$LOG"; fi
done

# 2. program mode: the 4 twins and the 9 trap/exit programs. Same stdin for both; exit status and every
#    output byte must be equal.
head -c 50000 /dev/urandom > $B/in.bin
pmode() { # prog stdin
  p=$1; in=$2; written=$((written + 1)); run=$((run + 1))
  $R/om4.bin < $X/$p.om > $B/$p.elf; chmod +x $B/$p.elf
  set +e
  ./$B/$p.elf < $in > $B/$p.om4.out; rc1=$?
  $I run $X/$p.om < $in > $B/$p.ref.out; rc2=$?
  set -e
  if [ $rc1 = $rc2 ] && cmp -s $B/$p.om4.out $B/$p.ref.out; then
    say "PASS $p: exit $rc1, $(wc -c < $B/$p.ref.out) output bytes, interpreter = om4 ELF"
  else bad "$p: om4 ELF exit $rc1 ($(wc -c < $B/$p.om4.out) B), interpreter exit $rc2 ($(wc -c < $B/$p.ref.out) B)"; fi
  [ -f $X/$p.want ] && { [ "$rc2" = "$(cat $X/$p.want)" ] || bad "$p: interpreter exit $rc2, the gate wants $(cat $X/$p.want)"; }
  return 0
}
for p in fib sort echo bytecopy; do pmode $p $B/in.bin; done
for t in $X/t_*.om; do pmode $(basename $t .om) /dev/null; done

# 3. the refusals of gate4 section 7, read from gate4.sh itself: om4's exit = the interpreter's = the gate's
t() { # code text tag
  written=$((written + 1)); run=$((run + 1))
  printf '%b' "$2" > $B/ctl.omegese
  set +e; $R/om4.bin < $B/ctl.omegese > /dev/null 2>&1; rc1=$?; $I check $B/ctl.omegese 2> $B/ctl.err; rc2=$?; set -e
  if [ $rc1 = "$1" ] && [ $rc2 = "$1" ]; then say "PASS refusal $1 [$3]: $(cat $B/ctl.err)"
  else bad "refusal [$3]: gate wants $1, om4 $rc1, interpreter $rc2: $(cat $B/ctl.err)"; fi
}
grep '^t [0-9]' $R/gate4.sh > $B/refusals.sh
. ./$B/refusals.sh

# 4. negative control: an interpreter with signed division made unsigned must differ from om4 on fm1
sed 's|return canon(t, (uint64_t)(a / b));|return canon(t, x / y);|' tools/omega_ref/omega_ref.c > $B/mut.c
cmp -s $B/mut.c tools/omega_ref/omega_ref.c && bad "mutation did nothing"
quiet-build cc -O2 -std=gnu11 -o $B/mut $B/mut.c -lpthread
if fmode fm1 sdivr $X/fm1.inputs $X/fm1.omegese $B/mut; then bad "mutant interpreter (signed div -> unsigned) not caught"
else say "PASS negative control: the interpreter with signed div made unsigned differs from om4 on fm1"; fi
fmode fm1 sdivr $X/fm1.inputs $X/fm1.omegese || bad "fm1 rerun"

say "cases written: $written, cases run: $run"
[ $written = $run ] || bad "cases run != cases written"
[ $fail = 0 ] && say "GATE_REF PASS" || say "GATE_REF FAIL"
exit $fail
