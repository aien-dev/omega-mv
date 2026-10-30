#!/bin/sh
# gate80.sh -- the checks of seed/sm80 (Ampere sm_80/86/87, Ada sm_89): asm2 (rung 2, unchanged) with
# sm80.tbl must write the exact words Mesa NAK's encoder writes. Run from the repo root:
#   sh seed/sm80/gate80.sh
# 1. the vectors (vectors/: listings and NAK's bytes, made by the nakref oracle, seed/sm80/README.md) are the
#    ones recorded in vectors/SHA256SUMS;
# 2. asm2 + sm80.tbl assembles the edge listing (every form, every guard predicate, each field at its edges,
#    each control field at its edges, branch distances, 64 random valid combinations per form); the words
#    must equal NAK's, bit for bit;
# 3. the hand-written vector add (vecadd.s2) assembles twice to the same bytes, and to NAK's bytes for the
#    same instructions (vectors/vecadd.nak.bin);
# 4. controls: a wrong register, a wrong offset, an out-of-range register, a wrong table bit and a field in
#    the wrong place must each change the vector add; a moved field in a form only the edge listing uses must
#    be caught by the edge check.
# Host only: no NVIDIA tool and no GPU is run; a pass is not hardware qualification. Results: evidence/SEED-0/sm80/.
set -eu
R=seed/sm80; V=$R/vectors; B=build/seed/sm80; E=evidence/SEED-0/sm80
ASM=seed/rung2/asm2.bin; TAB=$R/sm80.tbl
mkdir -p "$E" "$B"
fail=0
: > "$E/gate80.log"
say() { printf '%s\n' "$*" | tee -a "$E/gate80.log"; }
bad() { say "FAIL: $*"; fail=1; }
say "gate80 $(date -u +%FT%TZ) at $(git rev-parse HEAD)"
say "asm2 $(sha256sum $ASM | cut -d' ' -f1)  table $(sha256sum $TAB | cut -d' ' -f1)  ($(grep -c '^=' $TAB) lines)"

# 1. vectors
gzip -dc $V/edges.s2.gz > $B/edges.s2
gzip -dc $V/edges.nak.bin.gz > $B/edges.nak.bin
gzip -dc $V/cases.tsv.gz > $B/cases.tsv
cp $V/vecadd.nak.bin $B/vecadd.nak.bin
if (cd $B && sha256sum -c --quiet ../../../$V/SHA256SUMS); then say "PASS vectors match vectors/SHA256SUMS"
else bad "vectors differ from vectors/SHA256SUMS"; fi

# 2. edge cases
cat $TAB $B/edges.s2 | $ASM > $B/edges.a.bin 2> $B/edges.err || true
if cmp -s $B/edges.a.bin $B/edges.nak.bin; then
  say "PASS edges: $(( $(wc -c < $B/edges.nak.bin) / 16 )) instructions in $(wc -l < $B/cases.tsv) cases bit-identical to NAK"
else
  bad "edges differ: $(head -1 $B/edges.err) $(cmp $B/edges.a.bin $B/edges.nak.bin 2>&1 | head -1)"
fi

# 3. the hand-written vector add
cat $TAB $R/vecadd.s2 | $ASM > $R/vecadd.bin
cat $TAB $R/vecadd.s2 | $ASM > $B/vecadd.again
cmp -s $R/vecadd.bin $B/vecadd.again && say "PASS vecadd built by asm2 twice, same bytes" || bad "vecadd two builds differ"
cmp -s $R/vecadd.bin $B/vecadd.nak.bin && say "PASS vecadd.bin = NAK's words ($(stat -c %s $R/vecadd.bin) bytes)" \
  || bad "vecadd differs from NAK's words"
sha256sum $R/vecadd.bin | sed 's|  .*|  seed/sm80/vecadd.bin|' > "$E/vecadd.sha256"

# 4. controls
ctrl() { # description, sed edit, file (listing or table), listing to assemble, reference bytes
  sed "$2" "$3" > $B/ctrl.in
  if cmp -s $B/ctrl.in "$3"; then bad "control edit did nothing: $1"; return; fi
  if [ "$3" = $TAB ]; then cat $B/ctrl.in "$4"; else cat $TAB $B/ctrl.in; fi | $ASM > $B/ctrl.bin 2> $B/ctrl.err || true
  if cmp -s $B/ctrl.bin "$5"; then bad "control not caught: $1"; else say "PASS control caught: $1 ($(head -c 60 $B/ctrl.err))"; fi
}
VA=$R/vecadd.s2; VN=$B/vecadd.nak.bin
ctrl "wrong register (IADD3 R9 -> R8)" 's/^iadd3.0 7 9 2 /iadd3.0 7 8 2 /' $VA $VA $VN
ctrl "wrong offset (LDC.64 b 0x388 -> 0x38c)" 's/^cb.1 0x388 0$/cb.1 0x38c 0/' $VA $VA $VN
ctrl "register out of range (256)" 's/^ldc.0 7 1 255 /ldc.0 7 256 255 /' $VA $VA $VN
ctrl "wrong table bit (LDG word 2)" 's/^= ldg.2        0x080e0000/= ldg.2        0x080e0080/' $TAB $VA $VN
ctrl "wrong field position (S2R register at bit 9)" 's/^= s2r.2        0x00000000 i8.8 /= s2r.2        0x00000000 i9.7 /' $TAB $VA $VN
ctrl "edge-only form (SHFL clamp number at bit 41)" 's/^\(= shfl.ii.1 *0x00000000\) i8.13 /\1 i9.13 /' $TAB $B/edges.s2 $B/edges.nak.bin

if [ $fail = 0 ]; then say "GATE80 PASS"; else say "GATE80 FAIL"; fi
exit $fail
