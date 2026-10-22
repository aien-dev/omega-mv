#!/bin/sh
# gate_next.sh -- decision 22 over the om4 words: the grammar tools/omega_ref/s9.grammar gives exactly one
# parse at every prefix of every gate4 program (and of seed/omc/omc.om once it is on the branch), and it
# agrees with om4 and the interpreter on mutants. Run from the repo root: sh tools/omega_ref/gate_next.sh
# Results: evidence/OMEGA-REF/gate_next.log.
set -eu
R=seed/rung4; X=$R/tests; B=build/omega_ref/next; E=evidence/OMEGA-REF; G=tools/omega_ref/s9.grammar
N=build/omega_ref/omega_next; I=build/omega_ref/omega_ref
mkdir -p "$B" "$E"
LOG=$E/gate_next.log
: > "$LOG"
fail=0
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
bad() { say "FAIL: $*"; fail=1; }
say "gate_next $(date -u +%FT%TZ) at $(git rev-parse HEAD), grammar $(sha256sum $G | cut -c1-16)"
sh tools/omega_ref/build.sh

# the corpus: every accepted gate4 program, and omc.om when L408 has pushed it
git fetch -q origin seed/omega-in-omega 2>/dev/null || true
# function-mode sources without their free lines, as gate4 feeds them to om4 (om4's language has no free)
mkdir -p $B/src
for s in $X/*.omegese tests/one_layer/omegese/dev/dev*.omegese; do sed '/^free /d' $s > $B/src/$(basename $s); done
CORPUS="$(ls $X/*.om) $(ls $B/src/*.omegese)"
if git cat-file -e origin/seed/omega-in-omega:seed/omc/omc.om 2>/dev/null; then
  git show origin/seed/omega-in-omega:seed/omc/omc.om > $B/omc.om
  say "omc.om from origin/seed/omega-in-omega@$(git rev-parse --short origin/seed/omega-in-omega)"
  CORPUS="$CORPUS $B/omc.om"
else say "omc.om not on origin/seed/omega-in-omega yet (fuzz runs on gate4 programs only)"; fi

# 1. every word prefix: one parse, the real next word legal; every byte prefix: the cut word extendable
if $N fuzz $G $CORPUS > $B/fuzz.out; then say "PASS fuzz: $(tail -1 $B/fuzz.out), every one with exactly one parse"
else bad "fuzz"; grep FAIL $B/fuzz.out | tee -a "$LOG"; fi
grep -c '^PASS' $B/fuzz.out > /dev/null

# 2. the refused programs of gate4 section 7: up to where the grammar stops, never more than one parse
t() { # code text tag
  printf '%b' "$2" > $B/ctl.om
  set +e; $N accepts $G $B/ctl.om 2> $B/ctl.err; rc=$?; set -e
  case $rc in 0) say "PASS one parse, grammar takes it, refused later for a type or a rule [$3]";;
              1) say "PASS one parse up to the word the grammar refuses [$3]: $(cut -d: -f3- $B/ctl.err)";;
              *) bad "refusal program [$3]: rc $rc $(cat $B/ctl.err)";; esac
}
grep '^t [0-9]' $R/gate4.sh > $B/refusals.sh
. ./$B/refusals.sh

# 3. mutants: a program om4 or the interpreter accepts must parse; a mutant never has two parses
mk=0; gm=0; om=0; im=0; semantic=0; same=0
for f in $CORPUS; do
  [ "$f" = "$B/omc.om" ] && continue
  rm -f $B/mut/*.m* 2>/dev/null || true; mkdir -p $B/mut
  $N mutants $f 4453 20 $B/mut
  for m in $B/mut/*.m*; do
    mk=$((mk + 1))
    set +e
    $N accepts $G $m 2> /dev/null; g=$?
    $R/om4.bin < $m > /dev/null 2>&1; o=$?
    $I check $m 2> /dev/null; i=$?
    set -e
    [ $g = 0 ] && gm=$((gm + 1)); [ $o = 0 ] && om=$((om + 1)); [ $i = 0 ] && im=$((im + 1))
    [ $g = 0 ] && [ $o != 0 ] && semantic=$((semantic + 1))
    if { [ $o = 0 ] && [ $i != 0 ]; } || { [ $o != 0 ] && [ $i = 0 ]; }; then bad "mutant $(basename $m): om4 exit $o, interpreter $i (one accepts, the other refuses)"; mkdir -p $B/diffs; cp $m $B/diffs/; fi
    if [ $o = $i ]; then same=$((same + 1)); else mkdir -p $B/diffs; cp $m $B/diffs/; say "DIFF mutant $(basename $m): om4 refusal $o, interpreter $i (spec silent on the number, G25)"; fi
    [ $g -le 1 ] || bad "mutant $m: more than one parse"
    if [ $o = 0 ] && [ $g != 0 ]; then bad "om4 accepts a mutant the grammar refuses: $m"; cp $m $E/; fi
    if [ $i = 0 ] && [ $g != 0 ]; then bad "the interpreter accepts a mutant the grammar refuses: $m"; cp $m $E/; fi
  done
done
say "mutants: $mk; the grammar takes $gm, om4 $om, the interpreter $im; $semantic taken by the grammar and refused by om4 for a type or a rule"
say "mutants: om4 and the interpreter give the same verdict and refusal number on $same of $mk"

# 3b. the 125 probe programs of probe_ref.sh: every one the interpreter accepts parses with one parse
sh tools/omega_ref/probe_ref.sh > /dev/null 2>&1 || bad "probe_ref.sh"
pn=0; pa=0
for m in build/omega_ref/probe/*.om; do
  [ "$(basename $m)" = q.om ] && continue
  pn=$((pn + 1))
  set +e; $I check $m 2> /dev/null; i=$?; $N accepts $G $m 2> /dev/null; g=$?; set -e
  [ $g -le 1 ] || bad "probe $m: more than one parse"
  if [ $i = 0 ]; then pa=$((pa + 1)); [ $g = 0 ] || bad "the interpreter accepts probe $m, the grammar refuses it"; fi
done
say "PASS probes: $pa of $pn accepted by the interpreter, every one parses with one parse"
[ $mk -gt 0 ] || bad "no mutants"

# 4. negative controls: a grammar copy with an ambiguous rule must fail the fuzz with two parses; a copy
#    missing a rule must fail it with an illegal next word
sed 's/^          | VAR eqp value NL$/          | VAR eqp value NL\n          | VAR value NL/' $G > $B/amb1.grammar
sed 's/^          | .ret. retp NL$/          | '"'"'ret'"'"' retp NL\n          | '"'"'ret'"'"' NL/' $G > $B/amb2.grammar
sed 's/^value    -> NUM | .true. | .false. | VAR$/value    -> NUM | '"'"'true'"'"' | '"'"'false'"'"' | VAR | VAR/' $G > $B/amb3.grammar
grep -v "^          | 'exit' value NL" $G > $B/miss.grammar
for c in amb1 amb2 amb3; do
  cmp -s $B/$c.grammar $G && bad "control $c: the edit did nothing"
  if $N fuzz $B/$c.grammar $CORPUS > $B/$c.out; then bad "control $c (ambiguous grammar) passed the fuzz"
  elif grep -q 'MORE THAN ONE PARSE' $B/$c.out; then say "PASS negative control $c: $(grep -c 'MORE THAN ONE PARSE' $B/$c.out) programs caught with more than one parse"
  else bad "control $c failed for another reason: $(grep FAIL $B/$c.out | head -1)"; fi
done
cmp -s $B/miss.grammar $G && bad "control miss: the edit did nothing"
if $N fuzz $B/miss.grammar $CORPUS > $B/miss.out; then bad "control miss (no exit rule) passed the fuzz"
else say "PASS negative control miss (no exit rule): $(grep FAIL $B/miss.out | head -1)"; fi

[ $fail = 0 ] && say "GATE_NEXT PASS" || say "GATE_NEXT FAIL"
exit $fail
