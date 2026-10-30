#!/bin/sh
# gate5.sh -- the checks of rung 5, "Atlas reborn" (seed/README.md). Run from the repo root:
#   sh seed/atlas/gate5.sh [<atlas folder>]      (default ~/workspace/atlas, only read)
# 1. asm2 builds the Atlas image from seed/atlas/atlas.s2; a second build gives the same bytes;
# 2. it is byte-identical to the audited atlas.bin (sha256 f7802501...a5);
# 3. objdump (reference) reads every instruction back to its name, registers, numbers and labels;
#    planted mistakes are caught;
# 4. the existing Atlas M1 gates (run_m1_gates.sh, not edited) run in QEMU on a scratch copy of the
#    Atlas folder whose atlas.bin is the one asm2 made. The Atlas folder itself is never written.
# Results: evidence/SEED-0/rung5/.
set -eu
A=${1:-$HOME/workspace/atlas}
R=seed/atlas; B=build/seed/r5; E=evidence/SEED-0/rung5
PIN=f7802501b410a0c19eff7b8fca8865c9ba9c96bfa4f8065ca8f508b67748b9a5
mkdir -p "$B" "$E"
fail=0
: > "$E/gate5.log"
say() { printf '%s\n' "$*" | tee -a "$E/gate5.log"; }
bad() { say "FAIL: $*"; fail=1; }
say "gate5 $(date -u +%FT%TZ) at $(git rev-parse HEAD)"
TAB="seed/rung2/table.s2 seed/rung3/table3.s2 $R/table5.s2"
cat $TAB $R/atlas.s2 | seed/rung2/asm2.bin > $R/atlas.bin
cat $TAB $R/atlas.s2 | seed/rung2/asm2.bin > $B/atlas.again
cmp -s $R/atlas.bin $B/atlas.again && say "PASS built by asm2 twice, same bytes" || bad "two builds differ"
sha=$(sha256sum $R/atlas.bin | cut -d' ' -f1)
say "atlas.bin $(stat -c %s $R/atlas.bin) bytes sha256 $sha"
[ "$sha" = "$PIN" ] && say "PASS sha256 = audited atlas.bin" || bad "sha256 differs from the audited $PIN"
if [ -f "$A/atlas.bin" ]; then
  cmp $R/atlas.bin "$A/atlas.bin" > "$E/cmp.txt" 2>&1 && say "PASS byte-identical to $A/atlas.bin" || bad "bytes differ: $(head -1 $E/cmp.txt)"
fi
echo "$sha  seed/atlas/atlas.bin" > "$E/atlas.sha256"
if sh seed/check/audit2.sh $R/atlas.s2 $R/atlas.bin > $R/atlas.audit; then :; fi
tail -1 $R/atlas.audit | tee -a "$E/gate5.log" | grep -q 'mismatch=0$' && say "PASS audit (objdump reference run)" || bad "audit"
for c in 's/^  and.x.bm x20 x20 1 58 57 /  and.x.bm x20 x20 1 58 56 /' 's/^  ldp.w w13 w11 sp 25$/  ldp.w w13 w11 sp 26/' \
         's/^  adrp x13 0 0 /  adrp x13 1 0 /' 's/^  ldr.x.lit x19 L_40200000$/  ldr.x.lit x19 L_401fe100/' \
         's/^  extr.w w2 w3 w3 19$/  extr.w w2 w3 w2 19/'; do
  sed "$c" $R/atlas.s2 > $B/bad.s2
  if cmp -s $B/bad.s2 $R/atlas.s2; then bad "control edit did nothing: $c"
  elif sh seed/check/audit2.sh $B/bad.s2 $R/atlas.bin > $B/bad.audit; then bad "control not caught: $c"
  else say "PASS control caught ($c)"; fi
done
# the M1 gates, on a scratch copy (the gate script and its tools are the Atlas folder's own, unedited)
S=$(mktemp -d /tmp/seed-atlas-gate5.XXXXXX)
cp -a "$A/." "$S/"
cp $R/atlas.bin "$S/atlas.bin"
sha256sum "$S/run_m1_gates.sh" "$A/run_m1_gates.sh" | tee -a "$E/gate5.log"
set +e
(cd "$S" && sh ./run_m1_gates.sh) > "$E/m1-gates.log" 2>&1; rc=$?
set -e
say "run_m1_gates.sh exit $rc on the asm2-made atlas.bin (log: $E/m1-gates.log)"
[ $rc = 0 ] || bad "Atlas M1 gates"
say "scratch copy left at $S"
[ $fail = 0 ] && say "GATE5 PASS" || say "GATE5 FAIL"
exit $fail
