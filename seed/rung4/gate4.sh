#!/bin/sh
# gate4.sh -- the checks of rung 4 (seed/README.md). Run from the repo root:  sh seed/rung4/gate4.sh
# Needs build/omegese/omegese and build/omega_emit/omega_emit (the C path, built by make through
# quiet-build), cc for the reference runner and gcc for the C twins of the program tests. Oracles only:
# none of them writes a byte of seed/; om4.bin is made by asm2 alone. Results: evidence/SEED-0/rung4/.
set -eu
R=seed/rung4; B=build/seed/r4; E=evidence/SEED-0/rung4; X=$R/tests
T=tests/one_layer/exp445/dev/tasks/cases; O=tests/one_layer/omegese/dev
TAB="seed/rung2/table.s2 seed/rung3/table3.s2 $R/table4.s2"
mkdir -p "$B" "$E"
fail=0; cases=0
say() { printf '%s\n' "$*" | tee -a "$E/gate4.log"; }
bad() { say "FAIL: $*"; fail=1; }
: > "$E/gate4.log"
say "gate4 $(date -u +%FT%TZ) at $(git rev-parse HEAD)"

# 1. asm2 builds om4; a second build gives the same bytes; the file-size field matches the file
cat $TAB $R/om4.s | seed/rung2/asm2.bin > $B/om4.again
cmp -s $R/om4.bin $B/om4.again && say "PASS om4.bin = a fresh asm2 build of om4.s, same bytes" || bad "om4.bin is not what asm2 makes from om4.s"
size=$(stat -c %s $R/om4.bin); fsz=$(od -An -tu4 -j96 -N4 $R/om4.bin | tr -d ' ')
[ "$size" = "$fsz" ] && say "PASS p_filesz $fsz = file size $size" || bad "p_filesz $fsz, file $size"
sha256sum $R/om4.bin | tee "$E/om4.sha256" >> "$E/gate4.log"

# 2. every instruction of om4 decodes (objdump, reference) to its name, registers, numbers, label
if sh seed/check/audit2.sh $R/om4.s $R/om4.bin > $R/om4.audit; then :; fi
tail -1 $R/om4.audit | tee -a "$E/gate4.log" | grep -q 'mismatch=0$' && say "PASS audit (objdump reference run)" || bad "audit"
for c in 's/^  movz.x x25 0x181 1$/  movz.x x26 0x181 1/' 's/^  b.hs BO_SDIV$/  b.hs BO_RET/' \
         's/^  ldr.x.r x12 sp x11$/  ldr.x.r x12 sp x10/' 's/^  ubfm.x x11 x11 1 63$/  ubfm.x x11 x11 2 63/' \
         's/^  movz.x x0 0x63ff 0$/  movz.x x0 0x63fe 0/'; do
  sed "$c" $R/om4.s > $B/bad.s
  if cmp -s $B/bad.s $R/om4.s; then bad "control edit did nothing: $c"
  elif sh seed/check/audit2.sh $B/bad.s $R/om4.bin > $B/bad.audit; then bad "control not caught: $c"
  else say "PASS audit control caught ($c): $(tail -1 $B/bad.audit)"; fi
done

# 3. the reference runner (seed/check/runner.c, links the repo C path)
cc -O1 -Isrc -Isrc/compiler -o $B/runner seed/check/runner.c \
  $(ls src/compiler/*.c | grep -v oscc_main.c) src/sha256.c
[ -x build/omegese/omegese ] && [ -x build/omega_emit/omega_emit ] || { bad "C path tools not built"; exit 1; }

# 4. the 10 dev tasks (gate 3) under om4: the same words as om3, the same results as the C path and the
#    expected file
tab="$E/words.tsv"
printf 'program\tinputs\ttraps\tc_path_words\tom4_words\n' > "$tab"
cmpc() { # name entry inputs omegese -> compares om4 with the C path on the runner
  build/omegese/omegese emit $4 $B/$1.json > /dev/null
  build/omega_emit/omega_emit emit $B/$1.json $B/$1.oscir > /dev/null
  sed '/^free /d' $4 | $R/om4.bin > $B/$1.om4.bin # the C path's language needs free for its arrays; om4's has none
  $B/runner $B/$1.oscir "$2" $3 > $B/$1.c.out
  $B/runner $B/$1.oscir "$2" $3 $B/$1.om4.bin > $B/$1.s.out
  tail -n +2 $B/$1.c.out > $B/$1.c.res; tail -n +2 $B/$1.s.out > $B/$1.s.res
  n=$(wc -l < $3); tr=$(grep -c TRAP $B/$1.s.res || true); cases=$((cases + n))
  cw=$(sed -n 's/^words //p' $B/$1.c.out); sw=$(sed -n 's/^words //p' $B/$1.s.out)
  printf '%s\t%s\t%s\t%s\t%s\n' $1 $n $tr $cw $sw >> "$tab"
  if cmp -s $B/$1.c.res $B/$1.s.res; then say "PASS $1: $n inputs ($tr traps), om4 = C path (value and raw x0)"
  else bad "$1 differs from the C path"; diff $B/$1.c.res $B/$1.s.res | head -5 >> "$E/gate4.log"; fi
  grep -q NONCANONICAL $B/$1.s.res && bad "$1: a result not in canonical form" || true
  cp $B/$1.s.out $E/$1.om4.out; cp $B/$1.c.out $E/$1.c.out
}
for d in dev01 dev02 dev03 dev04 dev05 dev06 dev07 dev08 dev09 dev10; do
  entry=$(awk '$1=="entry"{print $2}' $T/$d/requirement)
  cmpc $d "$entry" $T/$d/hidden/inputs $O/$d.omegese
  cmp -s $B/$d.om4.bin evidence/SEED-0/rung3/$d.seed.bin && say "PASS $d: om4 words = om3 words" || bad "$d: om4 words differ from om3"
  cut -f1 $B/$d.s.res | cmp -s - $T/$d/hidden/expected && say "PASS $d: om4 = expected" || bad "$d differs from expected"
done

# 5. function-mode programs with the new words the C path can also express (signed div, rem, shl at
#    their limits, calls, a for with a value count, local arrays, array parameters, bounds traps)
for f in fm1 fm2 fm3 fm4 fm5 fm6 fm7 fm8 fm9; do
  cmpc $f "$(cat $X/$f.entry)" $X/$f.inputs $X/$f.omegese
  objdump -D -z -b binary -m aarch64 -M no-aliases $B/$f.om4.bin | grep -qE '	(udf|\.inst|undefined)' && bad "$f: a word that is not an instruction" || true
done
cat "$tab" >> "$E/gate4.log"

# 6. program mode (hand-laid ELF, no linker) against gcc-built C twins (gcc = oracle, named here):
#    recursive fib, a 1000-entry sort, echo stdin -> stdout, byte copy (reversed)
for p in fib sort echo bytecopy; do $R/om4.bin < $X/$p.om > $B/$p; chmod +x $B/$p; done
head -c 50000 /dev/urandom > $B/in.bin
for p in fib sort bytecopy; do gcc -O1 -o $B/$p.ref $X/$p.c; done
./$B/fib > $B/fib.out; ./$B/fib.ref > $B/fib.ref.out
cmp -s $B/fib.out $B/fib.ref.out && say "PASS fib (recursion): 26 lines = gcc twin" || bad "fib differs from its gcc twin"
./$B/sort > $B/sort.out; ./$B/sort.ref > $B/sort.ref.out
cmp -s $B/sort.out $B/sort.ref.out && say "PASS sort 1000: checksum, sorted count, first, last = gcc twin ($(tr '\n' ' ' < $B/sort.out))" || bad "sort differs"
./$B/echo < $B/in.bin > $B/echo.out
cmp -s $B/in.bin $B/echo.out && say "PASS echo: 50000 random bytes stdin -> stdout unchanged" || bad "echo"
./$B/bytecopy < $B/in.bin > $B/bc.out; ./$B/bytecopy.ref < $B/in.bin > $B/bc.ref.out
cmp -s $B/bc.out $B/bc.ref.out && say "PASS byte copy: 50000 bytes reversed = gcc twin" || bad "bytecopy"
cases=$((cases + 4))
for p in fib sort echo bytecopy; do cp $B/$p $E/$p.elf; done
#    program-mode traps and exit: status 100 + trap code, or the exit value
for t in $X/t_*.om; do n=$(basename $t .om)
  $R/om4.bin < $t > $B/$n; chmod +x $B/$n
  set +e; ./$B/$n < /dev/null; rc=$?; set -e; cases=$((cases + 1))
  [ "$rc" = "$(cat ${t%.om}.want)" ] && say "PASS $n: exit $rc" || bad "$n: exit $rc, want $(cat ${t%.om}.want)"
done

# 7. om4 refuses, each with its own number (decisions 16-24 of docs/PROGRAM.md marked)
t() { # expected-code text
  printf '%b' "$2" > $B/ctl.omegese; cases=$((cases + 1))
  set +e; $R/om4.bin < $B/ctl.omegese > $B/ctl.bin 2> $B/ctl.err; rc=$?; set -e
  [ "$rc" = "$1" ] && say "PASS refusal exit $rc: $(cat $B/ctl.err)  [$3]" || bad "refusal wanted exit $1, got $rc: $2"
}
t 3  'fn f a uint to uint\nret add a q\nend\n' 'unknown name'
t 7  'fn f a uint b ulong to ulong\nret add a b\nend\n' '20: no implicit widening'
t 7  'fn f a ulong to uint\nret a\nend\n' '20: no implicit narrowing'
t 16 'fn f a view uchar to uint\nret 0\nend\n' '16: an array without its length'
t 16 'global g uchar\nfn main to int\nret 0\nend\n' '16: a pool without its length'
t 19 'fn s a edit uint 4 b edit uint 4 to void\nend\nfn f to uint\nx = new uint 4 0\ns x x\nfree x\nret 0\nend\n' '18: one array twice in a call'
t 19 'global g uint 4\nfn s a view uint 4 to uint\nret get a 0\nend\nfn main to int\nr = s g\nret 0\nend\n' '18: a global passed'
t 20 'fn s a view uint 4 to void\nput a 0 1\nend\n' '17: write through a view'
t 20 'text t "ab"\nfn main to int\nput t 0 1\nret 0\nend\n' '17: write into a text'
t 20 'text t "ab"\nfn main to int\nn = read 0 t 0 1\nret 0\nend\n' '17: read into a text'
t 20 'fn s a edit uint 4 to void\nend\nfn f a view uint 4 to uint\ns a\nret 0\nend\n' '17: an edit argument from a view'
t 21 'fn main to int\nx = new uint 4 0\nr = get x 4\nret 0\nend\n' '23: a number index outside the array'
t 7  'fn s a view uint 4 to uint\nret 0\nend\nfn f to uint\nx = new uint 5 0\nr = s x\nret r\nend\n' '17: an array of another shape'
t 22 'text t "a\\qb"\nfn main to int\nret 0\nend\n' '19: an escape with no meaning'
t 22 'text t "ab\nfn main to int\nret 0\nend\n' 'a text with no end'
t 23 'global g ulong 16777217\nfn main to int\nret 0\nend\n' '24: a pool over its limit'
t 23 'fn main to int\nx = new ulong 600000 0\nret 0\nend\n' '24: local arrays over 4 MiB'
t 17 'fn f a uint to uint\nret a\nx = a\nend\n' 'a line after ret'
t 18 'fn f a uint to uint\nif eq a 0\nret 1\nend\nend\n' 'a value function that can reach its end'
t 9  'fn f a uint to uint\nret f a\nend\n' 'a self-call in function mode (stack use bounded by the call order)'
t 9  'global g uint 4\nfn f a uint to uint\nret a\nend\n' 'a pool in function mode'
t 9  'fn main to int\nif true\nx = new uint 4 0\nend\nret 0\nend\n' 'new inside a block'
t 9  'fn main a int to int\nret a\nend\n' 'main with parameters'
t 20 'fn s a edit int 2 to void\nput a 1 9\nend\nfn main to int\nx = new int 2 0\ns view x\nret get x 1\nend\n' '17: view A to an edit parameter (L412 probe view_pfx_to_edit)'
t 8  'fn main to int\nx = ulong -1\nret 0\nend\n' '20: a negative number is not a ulong'
t 8  'fn main to int\nx = long 9223372036854775808\nret 0\nend\n' '20: 2^63 is not a long'
t 8  'fn main to int\nx = byte 18446744073709551615\nret 0\nend\n' '20: 2^64 - 1 is not a byte (no wrap to -1)'
t 8  'fn main to int\nx = ulong 18446744073709551616\nret 0\nend\n' '23: 2^64 is no whole number of any type'
t 6  'fn main to int\nx = ulong 100000000000000000000\nret 0\nend\n' 'a number of 21 digits'
t 23 'global g uchar 9223372036854775808\nfn main to int\nret 0\nend\n' '24: a pool of 2^63 elements is over its limit'
t 9  'fn main to int\nx = new uint 4 0\nfree x\nret 0\nend\n' '24: free is not a word (no heap, nothing given back by hand)'
t 9  'fn f a int b int to int\nret 0\nend\nfn g to int\nx = own a\nret 0\nend\n' 'own is not in the subset'
t 10 'fn f a uint to uint\nret a\nend\nfn g f uint to uint\nret f\nend\n' 'a name equal to a function'
t 13 'fn f a uint to uint\nabcdefghi = a\nret a\nend\n' 'a variable over 8 characters'
t 15 'fn f a uint to uint\nret a\n' 'a function without end'
t 14 "fn f a ulong to ulong\nx = $(printf 'neg %.0s' $(seq 1025))a\nret x\nend\n" '23: values nested over 1024 deep (a capacity, refused not overrun)'
t 16 'end\n' 'end outside a function'
t 16 'ret 5\n' 'a statement outside a function'
t 6  'fn f a uint to uint\nret add a 12x\nend\n' 'bad number'
t 7  'fn f to void\nret\nend\nfn g to uint\nx = f\nret 0\nend\n' 'a void function used as a value'
t 7  'fn f to uint\nret 1\nend\nfn g to void\nf\nend\n' 'a value function called as a statement'

# 6b. names pass (R5b). A later function is a call, never a new scalar. Program mode may
#     recurse mutually. Function mode refuses a call cycle (error 9). Checked here, not against
#     the C path: the C path still has the one-pass limit these cases exist to leave behind.
r5b() { # file expect-exit expect-error-or-dash
  cases=$((cases + 1))
  set +e; $R/om4.bin < $X/$1 > $B/$1.bin 2> $B/$1.err; rc=$?; set -e
  if [ "$rc" != "$2" ]; then bad "$1: exit $rc, wanted $2 ($(cat $B/$1.err))"; return; fi
  if [ "$3" != "-" ] && ! grep -q "error $3" $B/$1.err; then bad "$1: stderr missing error $3: $(cat $B/$1.err)"; return; fi
  if [ "$3" = "-" ]; then say "PASS $1: exit $rc"; else say "PASS $1: exit $rc error $3"; fi
}
r5b r5b_fwd.om 0 -
chmod +x $B/r5b_fwd.om.bin; set +e; ./$B/r5b_fwd.om.bin >/dev/null; rc=$?; set -e
[ "$rc" = 7 ] && say "PASS r5b_fwd runs: exit 7 (the later function)" || bad "r5b_fwd ran as $rc, wanted 7"
r5b r5b_mut.om 0 -
chmod +x $B/r5b_mut.om.bin; set +e; ./$B/r5b_mut.om.bin >/dev/null; rc=$?; set -e
[ "$rc" = 0 ] && say "PASS r5b_mut runs: even 8 is 1, exit 0" || bad "r5b_mut ran as $rc, wanted 0"
r5b r5b_cycle.om 9 9
r5b r5b_assign.om 7 7
r5b r5b_fwd_fm.om 0 -
# the forward BL must land on late's MOVZ #7, not on a placeholder
objdump -D -b binary -m aarch64 -M no-aliases $B/r5b_fwd_fm.om.bin > $B/r5b_fwd_fm.dis
seven=$(awk '/movz/ && /#0x7/ { gsub(":", "", $1); print $1; exit }' $B/r5b_fwd_fm.dis)
if grep bl $B/r5b_fwd_fm.dis | grep -q "0x$seven"; then
  say "PASS r5b_fwd_fm: BL lands on MOVZ X0, #7 at 0x$seven"
else bad "r5b_fwd_fm: forward call did not land (see $B/r5b_fwd_fm.dis)"; fi

cases=$((cases + 3))

# 8. negative control on the corpus: an om4 with one word changed (signed division made unsigned) must
#    change a result
sed 's/^  movz.x x0 0x0c00 0$/  movz.x x0 0x0800 0/' $R/om4.s > $B/mut.s
cmp -s $B/mut.s $R/om4.s && bad "mutation did nothing"
cat $TAB $B/mut.s | seed/rung2/asm2.bin > $B/mut.bin; chmod +x $B/mut.bin
$B/mut.bin < $X/fm1.omegese > $B/fm1.mut.bin
$B/runner $B/fm1.oscir sdivr $X/fm1.inputs $B/fm1.mut.bin > $B/fm1.mut.out
if cmp -s $B/fm1.mut.out $B/fm1.s.out; then bad "mutant om4 (SDIV -> UDIV) not caught"; else say "PASS negative control: om4 with SDIV made UDIV changes fm1 results"; fi

# 9. the spec addendum (decision 19): no sentence says a meaning is left open
if sed -n "/^## 9. Systems words/,\$p" docs/one_layer/OMEGESE.md | grep -qi undefined; then bad "the word undefined in the spec addendum"; else say "PASS spec addendum (OMEGESE.md section 9): grep -i undefined finds nothing"; fi

# 10. decision 22, one legal next word: the next-word fuzz over the om4 words (seed/check/nextfuzz.sh).
#     NF_N random programs from seed/check/nextgen.awk (all must be accepted), each cut after every word
#     and changed in every word place: om4 answers each with exit 0 or its error number, never a crash.
#     (omegese next, the C tool, does not know the om4 words, so this fuzz is native.)
nf=${NF_N:-20}
if [ "$nf" -gt 0 ]; then
  if NFW=$B/nextfuzz sh seed/check/nextfuzz.sh $nf $R/om4.bin > $B/nextfuzz.out 2>&1; then say "PASS $(tail -1 $B/nextfuzz.out)"
  else bad "next fuzz: $(grep -v '^nextfuzz:' $B/nextfuzz.out | head -3) $(tail -1 $B/nextfuzz.out)"; fi
  cases=$((cases + 1))
fi
say "cases run: $cases"
[ $fail = 0 ] && say "GATE4 PASS" || say "GATE4 FAIL"
exit $fail
