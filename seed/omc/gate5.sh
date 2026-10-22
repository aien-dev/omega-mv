#!/bin/sh
# gate5.sh -- the checks of rung 5, omc (seed/README.md). Run from the repo root:  sh seed/omc/gate5.sh
# Needs seed/rung4/om4.bin (rung 4) and what gate4 needs: build/omegese/omegese and build/omega_emit/omega_emit
# (the C path), cc for the reference runner, gcc for the C twins. Oracles only, named where used: none of them
# writes a byte of seed/; omc is built by om4 and then by itself. Results: evidence/SEED-0/omc/.
set -eu
R=seed/omc; R4=seed/rung4; B=build/seed/r5; E=evidence/SEED-0/omc
TAB="seed/rung2/table.s2 seed/rung3/table3.s2 $R4/table4.s2"
mkdir -p "$B" "$E"
fail=0; cases=0
say() { printf '%s\n' "$*" | tee -a "$E/gate5.log"; }
bad() { say "FAIL: $*"; fail=1; }
: > "$E/gate5.log"
say "gate5 $(date -u +%FT%TZ) at $(git rev-parse HEAD)"
sha() { sha256sum "$1" | cut -d' ' -f1; }

# fix: stage1 = om4(src); stage2 = stage1(src); stage3 = stage2(src), in directory $1 (any stage may fail)
fix() { # dir src compiler
  rm -f $1/stage1 $1/stage2 $1/stage3
  $3 < $2 > $1/stage1 2> $1/err1 && chmod +x $1/stage1 &&
  ./$1/stage1 < $2 > $1/stage2 2> $1/err2 && chmod +x $1/stage2 &&
  ./$1/stage2 < $2 > $1/stage3 2> $1/err3 && chmod +x $1/stage3
}

# 1. (a) fixpoint
mkdir -p $B/fix; fix $B/fix $R/omc.om $R4/om4.bin || bad "a stage did not build: $(cat $B/fix/err* )"
s1=$(sha $B/fix/stage1); s2=$(sha $B/fix/stage2); s3=$(sha $B/fix/stage3)
say "stage1 = om4(omc.om)    $s1"
say "stage2 = stage1(omc.om) $s2"
say "stage3 = stage2(omc.om) $s3"
[ "$s2" = "$s3" ] && say "PASS (a) fixpoint: sha256(stage2) = sha256(stage3)" || bad "(a) stage2 != stage3"
cases=$((cases + 1))
S=$B/fix/stage2

# 2. (b) the second route. The C path (omegese + omega_emit, an oracle) cannot express omc.om; its own refusal
#    and the probes below say what is missing. So the fallback, which is not independent of this author's
#    design: two different compilers of omc.om, om4 (asm2 language) and stage1 (omc, Omegese), and the stage
#    each one's output builds from omc.om must be the same bytes.
build/omegese/omegese emit $R/omc.om $B/omc.json > $B/cpath.out 2>&1 || true
say "C path on omc.om: $(head -1 $B/cpath.out)"
probe() { # what text
  printf '%b' "$2" > $B/probe.om
  m=$(build/omegese/omegese emit $B/probe.om $B/probe.json 2>&1 | head -1)
  case "$m" in REFUSED*) say "  C path lacks $1: $m";; *) bad "C path now accepts $1: rethink (b)";; esac
}
probe "global pools" 'global g uchar 4\nfn f a ulong to ulong\nret a\nend\n'
probe "a self-call" 'fn f a ulong to ulong\nif eq a 0\nret 0\nend\nret f sub a 1\nend\n'
probe "arrays over 64 cells" 'fn f a ulong to ulong\nx = new uchar 100 0\nret a\nend\n'
probe "text literals" 'text t "ab"\nfn f a ulong to ulong\nret a\nend\n'
probe "read / write" 'fn f a ulong to ulong\nx = new uchar 4 0\nn = read 0 x 0 4\nret a\nend\n'
probe "exit" 'fn f a ulong to ulong\nexit int 3\nend\n'
probe "a colon in a note" 'note a: b\nfn f a ulong to ulong\nret a\nend\n'
say "  (and it writes .oscir for a runner, not a program that reads stdin and writes stdout)"
cmp -s $B/fix/stage1 $B/fix/stage2 && say "PASS (b) om4 and omc build the same omc from omc.om (stage1 = stage2)" || bad "(b) stage1 != stage2"
cmp -s $B/fix/stage2 $B/fix/stage3 && say "PASS (b) the stage from each parent is bit-identical (om4-built -> stage2, omc-built -> stage3)" || bad "(b) stage2 != stage3"
cases=$((cases + 2))

# 3. every input gate4 gives om4, plus every .om / .omegese file in the repo and omc.om: om4 and stage2
#    give the same output bytes, exit status and error number (and line); gate4 is run with om4 wrapped
#    so each input is kept (gate4's own results go to $B, not to evidence/)
C=$B/corpus; rm -rf $C $B/g4cap $B/g4cape; mkdir -p $C
cat > $B/cap.sh << EOF
#!/bin/sh
n=\$(ls $C | wc -l); f=$C/\$(printf '%04d' \$n).in; cat > \$f; exec $R4/om4.bin < \$f
EOF
chmod +x $B/cap.sh
g4() { # compiler build-dir evidence-dir -> gate4 with the compiler in om4's place
  sed -e "s|^R=seed/rung4; B=build/seed/r4; E=evidence/SEED-0/rung4|R=seed/rung4; B=$2; E=$3|" \
      -e "s#\\\$R/om4\\.bin < #$1 < #g" -e "s#| \\\$R/om4\\.bin #| $1 #g" $R4/gate4.sh > $2.sh
  [ "$(grep -c "$1" $2.sh)" = 4 ] || { bad "gate4 changed shape: the compiler is not in its 4 places"; return 1; }
  sh $2.sh > $2.out 2>&1
}
g4 $B/cap.sh $B/g4cap $B/g4cape || bad "gate4 (capturing) did not pass: $(grep FAIL $B/g4cap.out | head -3)"
for f in $(find $R4/tests tests/one_layer/omegese tools/exp445/arms/examples -name '*.om' -o -name '*.omegese' | sort) $R/omc.om; do
  cp $f $C/$(printf '%04d' $(ls $C | wc -l)).in
done
same=0; n=0
for f in $C/*.in; do n=$((n + 1))
  set +e; $R4/om4.bin < $f > $B/c.a 2> $B/c.ae; ra=$?; $S < $f > $B/c.b 2> $B/c.be; rb=$?; set -e
  sed -i 's/^om4:/X:/' $B/c.ae; sed -i 's/^omc:/X:/' $B/c.be
  if [ $ra = $rb ] && cmp -s $B/c.a $B/c.b && cmp -s $B/c.ae $B/c.be; then same=$((same + 1))
  else bad "corpus $(basename $f): om4 exit $ra $(cat $B/c.ae), stage2 exit $rb $(cat $B/c.be)"; fi
done
refused=$(for f in $C/*.in; do $R4/om4.bin < $f > /dev/null 2>&1 || echo x; done | wc -l)
[ $same = $n ] && say "PASS corpus: om4 = stage2 on $n inputs ($refused of them refused, same error number and line)"
cases=$((cases + n))

# 4. (c) gate4 with stage2 in om4's place: the 10 dev tasks and 9 function-mode tests against the C path
#    on the reference runner (raw x0, traps), the program-mode tests against gcc twins and their exits,
#    every refusal number
g4 $S $B/g4omc $B/g4omce || true
if grep -q '^FAIL' $B/g4omc.out || ! grep -q '^PASS' $B/g4omc.out; then bad "(c) gate4 with stage2: $(grep FAIL $B/g4omc.out | head -3)"
else say "PASS (c) gate4 with stage2 in om4's place: $(grep -c '^PASS' $B/g4omc.out) checks, 0 FAIL"; fi
cp $B/g4omce/words.tsv $E/corpus-words.tsv
sed 's/om4_words/omc_words/' $E/corpus-words.tsv > $B/t && mv $B/t $E/corpus-words.tsv
say "(c) table: evidence/SEED-0/omc/corpus-words.tsv"; cat $E/corpus-words.tsv >> "$E/gate5.log"
cases=$((cases + 1))

# 5. (d) negative controls
#    A: a one-word mutation omc itself uses (or compiled as and): the fixpoint must break, and the corpus
mk() { sed "$1" $R/omc.om > $2; cmp -s $2 $R/omc.om && { bad "mutation did nothing: $1"; return 1; }; return 0; }
diffs() { # compiler -> how many corpus inputs differ from om4 (by output, exit or error)
  k=0; for f in $C/*.in; do
    set +e; $R4/om4.bin < $f > $B/d.a 2>&1; ra=$?; timeout 10 $1 < $f > $B/d.b 2>&1; rb=$?; set -e
    sed -i 's/^om[4c]:/X:/' $B/d.a $B/d.b
    { [ $ra = $rb ] && cmp -s $B/d.a $B/d.b; } || k=$((k + 1)); done; echo $k; }
mkdir -p $B/ma; mk 's/^if eq op 39$/if eq op 40/' $B/ma/omc.om
if fix $B/ma $B/ma/omc.om $R4/om4.bin && cmp -s $B/ma/stage2 $B/ma/stage3; then bad "(d) A: fixpoint kept"
else say "PASS (d) A caught by the fixpoint: or -> and in omc.om (a stage failed, or stage2 != stage3)"; fi
k=$(diffs $B/ma/stage1); [ $k -gt 0 ] && say "PASS (d) A caught by the corpus: $k inputs differ from om4" || bad "(d) A: corpus blind"
#    B: a one-word mutation omc does not use (signed div compiled as unsigned): the fixpoint stays, the
#    corpus catches it
mkdir -p $B/mb; mk 's/^bo_w3 2596277248 rd ra rb 0$/bo_w3 2596276224 rd ra rb 0/' $B/mb/omc.om
fix $B/mb $B/mb/omc.om $R4/om4.bin && cmp -s $B/mb/stage2 $B/mb/stage3 && say "PASS (d) B keeps the fixpoint (a fixpoint proves determinism, not correctness)" || bad "(d) B: fixpoint broke"
k=$(diffs $B/mb/stage2); [ $k -gt 0 ] && say "PASS (d) B caught by the corpus: $k inputs differ from om4" || bad "(d) B: corpus blind"
#    C: the same wrong word in om4.s and omc.om (signed div as unsigned): fixpoint and om4 = omc both stay;
#    only the C path oracle catches it (fm2: -7 / 2)
mkdir -p $B/mc; mk 's/^bo_w3 2596277248 rd ra rb 0$/bo_w3 2596276224 rd ra rb 0/; s/^e3 2596277248 t ra rb$/e3 2596276224 t ra rb/' $B/mc/omc.om
sed 's/^  movz.x x0 0x0c00 0$/  movz.x x0 0x0800 0/' $R4/om4.s > $B/mc/om4.s
cat $TAB $B/mc/om4.s | seed/rung2/asm2.bin > $B/mc/om4.bin; chmod +x $B/mc/om4.bin
fix $B/mc $B/mc/omc.om $B/mc/om4.bin && cmp -s $B/mc/stage2 $B/mc/stage3 && say "PASS (d) C keeps the fixpoint" || bad "(d) C: fixpoint broke"
sed '/^free /d' $R4/tests/fm2.omegese > $B/mc/fm2.om
$B/mc/om4.bin < $B/mc/fm2.om > $B/mc/fm2.a; ./$B/mc/stage2 < $B/mc/fm2.om > $B/mc/fm2.b
cmp -s $B/mc/fm2.a $B/mc/fm2.b && say "PASS (d) C keeps om4 = omc on fm2" || bad "(d) C: om4' != omc' on fm2"
if g4 $B/mc/stage2 $B/g4mc $B/g4mce && ! grep -q '^FAIL' $B/g4mc.out; then bad "(d) C: the C path did not catch it"
else say "PASS (d) C caught only by the C path: $(grep '^FAIL' $B/g4mc.out | head -2 | tr '\n' ' ')"; fi
#    D: a malformed program is refused with an error number
printf 'fn f a uint to uint\nret add a q\nend\n' > $B/bad.om
set +e; $S < $B/bad.om > /dev/null 2> $B/bad.err; rc=$?; set -e
[ $rc = 3 ] && grep -q '^omc: line 2: error 3$' $B/bad.err && say "PASS (d) refusal: $(cat $B/bad.err), exit 3" || bad "(d) refusal: exit $rc $(cat $B/bad.err)"
cases=$((cases + 7))

# 6. output does not depend on addresses or time: stage2 run twice, with a different environment size (so
#    a different stack address) and a second later, gives the same bytes
env -i X=$(printf '%0999d' 0) $S < $R/omc.om > $B/det1; sleep 1; $S < $R/omc.om > $B/det2
cmp -s $B/det1 $B/det2 && cmp -s $B/det1 $S && say "PASS deterministic: same bytes under another environment and a second later" || bad "output depends on the run"
cases=$((cases + 1))

# 7. sizes and speed. omc's size at this self-rewrite; words per function of omc (from compiling each prefix
#    of omc.om with a 2-word main appended); compile time of stage2 against the C path on the dev tasks
lines=$(wc -l < $R/omc.om); nfn=$(grep -c '^fn ' $R/omc.om); bytes=$(stat -c %s $S)
printf 'rewrite\tbuilt_by\tsource_lines\tfunctions\tbytes\twords\tsha256\n' > $E/size.tsv
for st in 1 2 3; do by=om4; [ $st -gt 1 ] && by=stage$((st - 1))
  printf 'stage%s\t%s\t%s\t%s\t%s\t%s\t%s\n' $st $by $lines $nfn $(stat -c %s $B/fix/stage$st) $(( ($(stat -c %s $B/fix/stage$st) - 176) / 4 )) $(sha $B/fix/stage$st) >> $E/size.tsv
done
say "omc: $lines lines, $nfn functions, $bytes bytes = 176-byte header + $(( (bytes - 176) / 4 )) words (evidence/SEED-0/omc/size.tsv)"
printf 'fn main to int\nret 0\nend\n' > $B/m.om; printf 'fn zz to int\nret 0\nend\nfn main to int\nret 0\nend\n' > $B/zm.om
dm=$(( ($(./$S < $B/zm.om | wc -c) - $(./$S < $B/m.om | wc -c)) / 4 ))
awk '/^fn /{d=1; name=$2; next} d>0 && /^(if|while|for) /{d++} d>0 && /^end$/{d--; if(d==0) print NR, name}' $R/omc.om > $B/fnends
printf 'function\twords\n' > $E/omc-words.tsv; prev=$(./$S < $B/m.om | wc -c); start=1
while read end name; do
  if [ "$name" = main ]; then sz=$(./$S < $R/omc.om | wc -c); w=$(( (sz - prev) / 4 + dm ))
  else sz=$(sed -n "1,${end}p" $R/omc.om | cat - $B/m.om | ./$S | wc -c); w=$(( (sz - prev) / 4 )); fi
  printf '%s\t%s\n' $name $w >> $E/omc-words.tsv; prev=$sz
done < $B/fnends
tw=$(awk 'NR>1{s+=$2} END{print s}' $E/omc-words.tsv)
say "omc words per function: evidence/SEED-0/omc/omc-words.tsv ($nfn functions, $tw words in all, mean $((tw / nfn)))"
ns() { date +%s%N; }
N=200; printf 'program\tomc_us\tc_path_us\n' > $E/compile-time.tsv
for d in dev01 dev02 dev03 dev04 dev05 dev06 dev07 dev08 dev09 dev10; do
  sed '/^free /d' tests/one_layer/omegese/dev/$d.omegese > $B/t.om
  t0=$(ns); i=0; while [ $i -lt $N ]; do $S < $B/t.om > /dev/null; i=$((i + 1)); done; t1=$(ns)
  i=0; while [ $i -lt $N ]; do build/omegese/omegese emit tests/one_layer/omegese/dev/$d.omegese $B/t.json > /dev/null
    build/omega_emit/omega_emit emit $B/t.json $B/t.oscir > /dev/null; i=$((i + 1)); done; t2=$(ns)
  printf '%s\t%s\t%s\n' $d $(( (t1 - t0) / N / 1000 )) $(( (t2 - t1) / N / 1000 )) >> $E/compile-time.tsv
done
N=20; t0=$(ns); i=0; while [ $i -lt $N ]; do $S < $R/omc.om > /dev/null; i=$((i + 1)); done; t1=$(ns)
say "compile time per program, microseconds, $((200)) runs each, process start included (evidence/SEED-0/omc/compile-time.tsv); omc compiling itself: $(( (t1 - t0) / N / 1000 )) us"
cat $E/compile-time.tsv $E/size.tsv >> "$E/gate5.log"

cp $S $E/omc.elf; sha $S > $E/omc.sha256
say "cases run: $cases"
[ $fail = 0 ] && say "GATE5 PASS" || say "GATE5 FAIL"
exit $fail
