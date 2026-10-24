#!/bin/sh
# gate_ref.sh -- the reference interpreter (tools/omega_ref, an oracle outside the seed chain) against the
# om4-made programs on every gate4 case. Run from the repo root:  sh tools/omega_ref/gate_ref.sh
# Needs build/omegese/omegese and build/omega_emit/omega_emit (only to make the .oscir that the referee
# runner reads parameter types from), cc. Reads $SEED (default seed) and the dev tasks under tests/ only.
# SEED=<dir> points it at another seed tree (om4.bin, gate4.sh, tests, check/runner.c); EVID=<dir> is the
# results dir (default evidence/OMEGA-REF): gate_ref.log.
set -eu
SEED=${SEED:-seed}; R=$SEED/rung4; X=$R/tests; B=build/omega_ref/gate; E=${EVID:-evidence/OMEGA-REF}
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
quiet-build cc -O1 -Isrc -Isrc/compiler -o $B/runner $SEED/check/runner.c \
  $(ls src/compiler/*.c | grep -v oscc_main.c) src/sha256.c

# 1. function mode: the 10 dev tasks and fm1-9. om4's words run by the referee runner; the interpreter
#    calls the entry itself. Every input line is a case: value and raw x0, or the trap number.
fmode() { # name entry inputs source [interpreter] [source for the C path, default source]
  interp=${5:-$I}; csrc=${6:-$4}
  build/omegese/omegese emit $csrc $B/$1.json > /dev/null
  build/omega_emit/omega_emit emit $B/$1.json $B/$1.oscir > /dev/null
  sed '/^free /d' $4 > $B/$1.src # as gate4: the C path needs free for its arrays; om4's language has none
  $R/om4.bin < $B/$1.src > $B/$1.om4.bin
  $B/runner $B/$1.oscir "$2" $3 $B/$1.om4.bin | tail -n +2 > $B/$1.om4.res
  $interp call $B/$1.src "$2" $3 > $B/$1.ref.res
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

# 2b. the names pass (gate4 section 6b, R5b; OMEGESE.md 9.2): a function written later is a call; program
#     mode may recurse mutually; function mode refuses a call cycle (9); a later function's name is never a
#     new scalar (7). om4's exit = the interpreter's = gate4's on every one.
if [ -f $X/r5b_fwd.om ]; then
  pmode r5b_fwd /dev/null; [ "$rc2" = 7 ] || bad "r5b_fwd: interpreter exit $rc2, gate4 wants 7 (the later function)"
  pmode r5b_mut /dev/null; [ "$rc2" = 0 ] || bad "r5b_mut: interpreter exit $rc2, gate4 wants 0 (even 8 is 1)"
  rfile() { # file want
    written=$((written + 1)); run=$((run + 1))
    set +e; $R/om4.bin < $X/$1 > /dev/null 2>&1; rc1=$?; $I check $X/$1 2> $B/ctl.err; rc2=$?; set -e
    if [ $rc1 = "$2" ] && [ $rc2 = "$2" ]; then say "PASS refusal $2 [$1]: $(cat $B/ctl.err)"
    else bad "refusal [$1]: gate4 wants $2, om4 $rc1, interpreter $rc2: $(cat $B/ctl.err)"; fi
  }
  rfile r5b_cycle.om 9
  rfile r5b_assign.om 7
  # function mode: entry calls mid, mid calls the later late, which gives 7. The C path reads one pass and
  # refuses the forward call, so the .oscir (the runner takes only the entry's types from it) comes from the
  # same three functions written in call order; om4 and the interpreter read the test as written.
  printf 'fn late to int\nret 7\nend\nfn mid to int\nret late\nend\nfn entry to int\nret mid\nend\n' > $B/r5b_fwd_fm.ordered.om
  sort $X/r5b_fwd_fm.om > $B/r5b_a; sort $B/r5b_fwd_fm.ordered.om > $B/r5b_b
  cmp -s $B/r5b_a $B/r5b_b || bad "r5b_fwd_fm: the ordered copy for the C path has drifted from the test"
  printf '\n' > $B/none.inputs # one case, no parameters
  written=$((written + 1)); run=$((run + 1))
  if fmode r5b_fwd_fm entry $B/none.inputs $X/r5b_fwd_fm.om $I $B/r5b_fwd_fm.ordered.om; then
    v=$(cut -f1 $B/r5b_fwd_fm.ref.res)
    [ "$v" = 7 ] && say "PASS r5b_fwd_fm: entry = 7 through the later function, interpreter = om4" || bad "r5b_fwd_fm: entry gives $v, gate4 wants 7"
  else bad "r5b_fwd_fm: interpreter differs from om4"; diff $B/r5b_fwd_fm.om4.res $B/r5b_fwd_fm.ref.res | head -5 | tee -a "$LOG"; fi
else say "NOT_RUN names pass (2b): no r5b tests in $X (a seed from before the names pass)"; fi

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
