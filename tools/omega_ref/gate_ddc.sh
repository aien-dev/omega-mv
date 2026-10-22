#!/bin/sh
# gate_ddc.sh -- the third parent of diverse double-compiling (DDC) for omc, the Omega compiler written in
# Omega (L408). Stage 1 is om4(omc.om); stage 1 applied to omc.om gives stage 2. The reference interpreter
# (tools/omega_ref, written from the spec, not from om4's code generator) runs omc.om on omc.om directly.
# Its output must equal stage 2 byte for byte: a miscompile that om4 and stage 1 share would show here.
# Run from the repo root: sh tools/omega_ref/gate_ddc.sh [omc.om]   (default: omc.om from
# origin/seed/omega-in-omega). Results: evidence/OMEGA-REF/gate_ddc.log and ddc.sha256.
set -eu
R=seed/rung4; B=build/omega_ref/ddc; E=evidence/OMEGA-REF; I=build/omega_ref/omega_ref
mkdir -p "$B" "$E"
LOG=$E/gate_ddc.log
: > "$LOG"
fail=0
say() { printf '%s\n' "$*" | tee -a "$LOG"; }
bad() { say "FAIL: $*"; fail=1; }
sh tools/omega_ref/build.sh
if [ $# -ge 1 ]; then cp "$1" $B/omc.om; from="$1"
else
  git fetch -q origin seed/omega-in-omega
  git show origin/seed/omega-in-omega:seed/omc/omc.om > $B/omc.om
  from="origin/seed/omega-in-omega@$(git rev-parse origin/seed/omega-in-omega)"
  git show origin/seed/omega-in-omega:seed/rung4/om4.bin > $B/om4.bin; chmod +x $B/om4.bin
  R=$B
fi
OM4=$R/om4.bin
say "gate_ddc $(date -u +%FT%TZ) at $(git rev-parse HEAD); omc.om from $from"
say "om4.bin $(sha256sum $OM4 | cut -d' ' -f1)  omc.om $(sha256sum $B/omc.om | cut -d' ' -f1)"

# stage 1: om4 builds omc; stage 2: stage 1 compiles omc.om
set +e; $OM4 < $B/omc.om > $B/stage1 2> $B/stage1.err; rc=$?; set -e
[ $rc = 0 ] || { bad "om4 refuses omc.om: exit $rc $(cat $B/stage1.err)"; exit 1; }
chmod +x $B/stage1
set +e; ./$B/stage1 < $B/omc.om > $B/stage2 2> $B/stage2.err; rc1=$?; set -e
say "stage 2 = stage1(omc.om): exit $rc1, $(wc -c < $B/stage2) bytes"

# the third parent: the interpreter runs omc.om with omc.om as its input
t0=$(date +%s)
set +e; $I run $B/omc.om < $B/omc.om > $B/ref.out 2> $B/ref.err; rc2=$?; set -e
t1=$(date +%s)
say "interpreter: omc.om run on omc.om: exit $rc2, $(wc -c < $B/ref.out) bytes, $((t1 - t0)) s"
s2=$(sha256sum < $B/stage2 | cut -d' ' -f1); sr=$(sha256sum < $B/ref.out | cut -d' ' -f1)
printf 'stage2 %s\ninterp %s\nom4 %s\nomc.om %s\n' "$s2" "$sr" "$(sha256sum < $OM4 | cut -d' ' -f1)" "$(sha256sum < $B/omc.om | cut -d' ' -f1)" > $E/ddc.sha256
say "sha256 stage2 $s2"
say "sha256 interp $sr"
if [ $rc1 = $rc2 ] && cmp -s $B/stage2 $B/ref.out; then say "PASS DDC third parent: interp(omc.om, omc.om) = stage1(omc.om), byte for byte, same exit $rc1"
else
  bad "DDC mismatch: stage 2 exit $rc1, interpreter exit $rc2"
  first=$(cmp $B/stage2 $B/ref.out 2>&1 | head -1 || true)
  say "first difference: $first"
  cmp -l $B/stage2 $B/ref.out 2> /dev/null | head -8 | tee -a "$LOG" || true
fi

# fixpoint as a side check: stage 2 compiles omc.om to itself (L408's own gate is the authority)
if [ $rc1 = 0 ] && [ -s $B/stage2 ]; then
  chmod +x $B/stage2
  set +e; ./$B/stage2 < $B/omc.om > $B/stage3 2> /dev/null; rc3=$?; set -e
  cmp -s $B/stage2 $B/stage3 && say "side check: stage3 = stage2 (exit $rc3)" || say "side check: stage3 differs from stage2 (exit $rc3), L408's gate decides"
fi

# negative controls: a mutant interpreter (unsigned shl off by one bit) must change the output, and a
# one-byte change of stage 2 must be caught by the comparison
sed 's|    if (!tsigned(t)) return canon(t, x << s);|    if (!tsigned(t)) return canon(t, (x << s) ^ (s == 3));|' tools/omega_ref/omega_ref.c > $B/mut.c
cmp -s $B/mut.c tools/omega_ref/omega_ref.c && bad "mutation did nothing"
quiet-build cc -O2 -std=gnu11 -o $B/mut $B/mut.c -lpthread
set +e; $B/mut run $B/omc.om < $B/omc.om > $B/mut.out 2> /dev/null; set -e
if cmp -s $B/mut.out $B/stage2; then bad "negative control: the mutant interpreter (shl) gives stage 2 too"
else say "PASS negative control: the mutant interpreter (unsigned shl by 3 flips bit 0) differs from stage 2"; fi
cp $B/stage2 $B/flip; printf '\377' | dd of=$B/flip bs=1 seek=100 conv=notrunc 2> /dev/null
if cmp -s $B/flip $B/ref.out; then bad "negative control: a flipped byte is not caught"; else say "PASS negative control: one changed byte of stage 2 is caught"; fi

[ $fail = 0 ] && say "GATE_DDC PASS" || say "GATE_DDC FAIL"
exit $fail
