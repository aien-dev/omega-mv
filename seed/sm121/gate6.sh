#!/bin/sh
# gate6.sh -- the checks of rung 6, "GPU words from the same chain" (seed/sm121/README.md). Run from the repo root:
#   sh seed/sm121/gate6.sh
# 1. the reference tool (sm121list, built from the repo's own GPU code generators, never part of seed/) writes every
#    program the generators make over a sweep of their settings, as an asm2 listing plus the C encoder's bytes;
# 2. asm2 (rung 2, unchanged) + sm121.tbl assembles every listing; the words must equal the C encoder's, bit for bit;
# 3. the same for every instruction form with registers and numbers at the edges of their fields;
# 4. the hand-written vector add (vecadd.s2) assembles to the exact bytes of omega_blackwell_encode_vecadd;
# 5. controls: a wrong register, a wrong table bit and an out-of-range number are caught;
# 6. reference only: nvdisasm -b SM121 reads each instruction form back; its name is compared with the name
#    the C encoder gives it (names.tsv). Any difference is listed, never hidden.
# Results: evidence/SEED-0/sm121/.
set -eu
R=seed/sm121; B=build/seed/r6; E=evidence/SEED-0/sm121; O=$B/out
ASM=seed/rung2/asm2.bin; TAB=$R/sm121.tbl
NVD=${NVDISASM:-/usr/local/cuda/bin/nvdisasm}
mkdir -p "$E" "$O/k"
fail=0
: > "$E/gate6.log"
say() { printf '%s\n' "$*" | tee -a "$E/gate6.log"; }
bad() { say "FAIL: $*"; fail=1; }
say "gate6 $(date -u +%FT%TZ) at $(git rev-parse HEAD)"
say "asm2 $(sha256sum $ASM | cut -d' ' -f1)  table $(sha256sum $TAB | cut -d' ' -f1)  ($(grep -c '^=' $TAB) lines)"
[ -x $B/sm121list ] || { bad "reference tool $B/sm121list not built (see README: build)"; exit 1; }
rm -f "$O"/k/*.s2 "$O"/k/*.bin
rm -f "$O/kernels.tsv" "$O/vecadd.c.bin" "$O/edges.s2" "$O/edges.c.bin"
$B/sm121list kernels "$O" | tee -a "$E/gate6.log"
$B/sm121list edges "$O" | tee -a "$E/gate6.log"

# 2. every kernel
n=0; bads=0; : > "$E/kernels-result.tsv"
for s in "$O"/k/*.s2; do
  k=${s%.s2}; name=$(basename "$k")
  if cat $TAB "$s" | $ASM > "$k.a.bin" 2> "$k.err" && cmp -s "$k.a.bin" "$k.c.bin"; then r=SAME
  else r="DIFF $(cat "$k.err" 2>/dev/null | head -1) $(cmp "$k.a.bin" "$k.c.bin" 2>&1 | head -1)"; bads=$((bads+1)); fi
  printf '%s\t%s\t%s\n' "$name" "$(sha256sum "$k.c.bin" | cut -c1-16)" "$r" >> "$E/kernels-result.tsv"
  n=$((n+1))
done
say "kernels: $n programs, $((n-bads)) bit-identical, $bads differ ($E/kernels-result.tsv)"
[ $n -gt 0 ] && [ $bads = 0 ] || bad "kernel words differ"
words=$(cat "$O"/k/*.c.bin | wc -c)
say "kernels: $((words/16)) instruction words compared ($words bytes)"

# 3. edge cases
cat $TAB "$O/edges.s2" | $ASM > "$O/edges.a.bin" 2> "$O/edges.err" || true
if cmp -s "$O/edges.a.bin" "$O/edges.c.bin"; then say "PASS edges: $(( $(wc -c < "$O/edges.c.bin") / 16 )) instruction cases bit-identical"
else bad "edges differ: $(head -1 "$O/edges.err") $(cmp "$O/edges.a.bin" "$O/edges.c.bin" 2>&1 | head -1)"; fi

# 4. the hand-written vector add
cat $TAB $R/vecadd.s2 | $ASM > $R/vecadd.bin
cat $TAB $R/vecadd.s2 | $ASM > $B/vecadd.again
cmp -s $R/vecadd.bin $B/vecadd.again && say "PASS vecadd built by asm2 twice, same bytes" || bad "vecadd two builds differ"
cmp -s $R/vecadd.bin "$O/vecadd.c.bin" && say "PASS vecadd.bin = omega_blackwell_encode_vecadd ($(stat -c %s $R/vecadd.bin) bytes)" || bad "vecadd differs from the C words"
sha256sum $R/vecadd.bin | sed 's|  .*|  seed/sm121/vecadd.bin|' > "$E/vecadd.sha256"

# 5. controls
ctrl() { # description, sed edit, file (listing or table)
  sed "$2" "$3" > $B/ctrl.in
  if cmp -s $B/ctrl.in "$3"; then bad "control edit did nothing: $1"; return; fi
  if [ "$3" = $TAB ]; then cat $B/ctrl.in $R/vecadd.s2; else cat $TAB $B/ctrl.in; fi | $ASM > $B/ctrl.bin 2> $B/ctrl.err || true
  if cmp -s $B/ctrl.bin "$O/vecadd.c.bin"; then bad "control not caught: $1"; else say "PASS control caught: $1 ($(head -c 60 $B/ctrl.err))"; fi
}
ctrl "wrong register (IADD3 R9 -> R8)" 's/^  iadd3.0 9 2$/  iadd3.0 8 2/' $R/vecadd.s2
ctrl "wrong offset (LDC.64 b 226 -> 227)" 's/^  ldc.64.1 226$/  ldc.64.1 227/' $R/vecadd.s2
ctrl "register out of range (256)" 's/^  ldc.0 1$/  ldc.0 256/' $R/vecadd.s2
ctrl "wrong table bit (LDG.E word 2)" 's/^= ldg.2        0x0c1e1900$/= ldg.2        0x0c1e1980/' $TAB
ctrl "wrong field position (S2R register at bit 9)" 's/^= s2r.2        0x00000000 i8.8$/= s2r.2        0x00000000 i9.7/' $TAB

# 6. reference: the disassembler's name for every instruction form (one word per distinct first-three-lines key)
if [ -x "$NVD" ]; then
  say "nvdisasm: $("$NVD" --version | grep -o 'release [0-9.]*, V[0-9.]*')  (reference reader only)"
  awk '/^  /{ split($0, f, " "); n++; key = key (key == "" ? "" : "+") f[1]; if (n % 4 == 3) { print key; key = "" } if (n % 4 == 0) key = "" }' "$O/edges.s2" > $B/edges.keys
  # nvdisasm prints nothing for a file holding any word it refuses, so the refused instructions (it names each by
  # address) are taken out and it reads the rest; repeated until it refuses none. Refusals are counted by kind.
  od -An -v -tx1 -w16 "$O/edges.a.bin" | tr -d ' ' | awk '{ print NR - 1 "\t" $0 }' > $B/edges.hex
  awk '/^  /{ n++; if (n % 4 == 0) { split($0, f, " "); print (f[1] == "ctl" ? f[2] : "default") } }' "$O/edges.s2" > $B/edges.ctl
  paste $B/edges.keys $B/edges.hex $B/edges.ctl | awk -F'\t' '{ print $2 "\t" $1 "\t" $3 "\t" $4 }' > $B/nvd.in   # index, key, hex, schedule word
  : > $B/nvd.refused; : > $B/nvd.refused.ctl
  for round in 1 2 3 4 5; do
    cut -f3 $B/nvd.in | xxd -r -p > $B/nvd.bin
    "$NVD" -b SM121 $B/nvd.bin > $B/nvd.out 2> $B/nvd.err || true
    grep -q 'at address' $B/nvd.err || break
    sed -n 's/.* at address 0x\([0-9a-f]*\).*/\1/p' $B/nvd.err | while read -r a; do echo $(( 0x$a / 16 )); done | sort -n -u > $B/nvd.bad
    awk -F'\t' 'NR == FNR { bad[$1]; next } (FNR - 1) in bad { print $4 "\t" $2 }' $B/nvd.bad $B/nvd.in >> $B/nvd.refused.ctl
    sed 's/ at address.*//' $B/nvd.err >> $B/nvd.refused
    awk -F'\t' 'NR == FNR { bad[$1]; next } !((FNR - 1) in bad)' $B/nvd.bad $B/nvd.in > $B/nvd.next && mv $B/nvd.next $B/nvd.in
  done
  grep -q 'at address' $B/nvd.err && bad "nvdisasm still refuses words after 5 rounds"
  grep -E '^ +/\*[0-9a-f]{4,}\*/' $B/nvd.out | sed 's|^ */\*[0-9a-f]*\*/ *||; s/ *;.*//' > $B/edges.sass
  cut -f2 $B/nvd.in | paste - $B/edges.sass > $B/edges.pairs
  sort $B/nvd.refused | uniq -c | sort -rn > "$E/nvdisasm-refused.txt"
  cut -f1 $B/nvd.refused.ctl | sed "s/^/schedule word /" | sort | uniq -c | sort -rn > "$E/nvdisasm-refused-schedule.txt"
  awk -F'\t' '$1 != "0xffffffff" { print $2 }' $B/nvd.refused.ctl | sort | uniq -c | sort -rn > "$E/nvdisasm-refused-forms.txt"
  say "nvdisasm refusals by schedule word: $(awk '{ printf "%s%s x%s", s, $4, $1; s = ", " }' "$E/nvdisasm-refused-schedule.txt")"
  say "nvdisasm read $(wc -l < $B/edges.sass) of $(wc -l < $B/edges.keys) edge instructions; refused $(wc -l < $B/nvd.refused.ctl) ($E/nvdisasm-refused.txt; with an ordinary schedule word, by form: $E/nvdisasm-refused-forms.txt)"
  # the disassembler's opcode (first token after an optional @P guard) for each key, every distinct pairing kept
  awk -F'\t' '{ s = $2; sub(/^@!?P[T0-9] +/, "", s); split(s, t, " "); print $1 "\t" t[1] }' $B/edges.pairs | sort -u > "$E/forms-nvdisasm.tsv"
  : > "$E/name-differences.tsv"
  while IFS='	' read -r key want; do
    case $key in \;*|'') continue;; esac
    got=$(awk -F'\t' -v k="$key" '$1 == k { print $2 }' "$E/forms-nvdisasm.tsv" | sort -u | tr '\n' ' ' | sed 's/ $//')
    [ -n "$got" ] || { printf '%s\t%s\t%s\n' "$key" "$want" "(no case)" >> "$E/name-differences.tsv"; continue; }
    [ "$got" = "$want" ] || printf '%s\t%s\t%s\n' "$key" "$want" "$got" >> "$E/name-differences.tsv"
  done < $R/names.tsv
  nd=$(grep -vc '(no case)' "$E/name-differences.tsv" || true)
  say "names: $(grep -vc '^;' $R/names.tsv) forms checked, $nd where the C encoder's name and the disassembler's differ ($E/name-differences.tsv)"
  "$NVD" -b SM121 $R/vecadd.bin > "$E/vecadd.sass.txt" 2>&1 || bad "nvdisasm could not read vecadd.bin"
else
  say "NOT_RUN nvdisasm reference: $NVD missing"
fi
cp "$O/kernels.tsv" "$E/kernels.tsv"; sort "$O/notes.tsv" | awk -F'\t' '{ c[$3]++ } END { for (k in c) print c[k] "\t" k }' | sort -rn > "$E/notes-summary.tsv"
say "notes (what the C encoder silently ignores or drops): $(wc -l < "$E/notes-summary.tsv") kinds, $E/notes-summary.tsv"
[ $fail = 0 ] && say "GATE6 PASS" || say "GATE6 FAIL"
exit $fail
