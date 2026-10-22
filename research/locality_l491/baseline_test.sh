#!/bin/sh
# Compile the pinned allocator's own expressions as an independent layout oracle.
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
root=$PWD
cat > "$out/baseline/layout_oracle.c" <<'C'
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "omega_gpu_decode_graph.h"
#include "omega_blackwell_codegen_dec.h"
static size_t al(size_t n){return (n+255u)&~(size_t)255u;}
int main(void){
 struct {uint32_t layers;} c={48},*cfg=&c;
 struct {uint32_t am_ctas;} state={(151936+OMEGA_DEC_ARGMAX_SPAN-1)/OMEGA_DEC_ARGMAX_SPAN},*g=&state;
 struct {size_t x,xq,qkv,attn,o,gu,act,dn,hidden,logits,am_v,am_i,token,norms,fnorm,tabs,pf_x,pf_y;} storage,*L=&storage;
 const size_t H=OMEGA_GPU_DGRAPH_HIDDEN,TK=OMEGA_GPU_DGRAPH_TOPK,I=OMEGA_GPU_DGRAPH_INTER;
 size_t o=0;
C
sed -n '/^#define REG(f, bytes)/,/^#undef REG/p' "$out/baseline/source/omega_gpu_decode_graph.c" >> "$out/baseline/layout_oracle.c"
# Reproduce each offset in field order, from the actual source expressions.
for name in x xq qkv attn o gu act dn hidden logits am_v am_i token norms fnorm tabs pf_x pf_y; do
 printf 'printf("%s\\t%%zu\\n",L->%s);\n' "$name" "$name" >> "$out/baseline/layout_oracle.c"
done
printf 'printf("TOTAL\\t%%zu\\n",o);return 0;}\n' >> "$out/baseline/layout_oracle.c"
cc -std=c11 -Wall -Wextra -Werror -I"$out/baseline/source" -Isrc "$out/baseline/layout_oracle.c" -o "$out/baseline/layout_oracle"
"$out/baseline/layout_oracle" > "$out/baseline/layout-expected.tsv"
awk -F '\t' 'NR>1 {print $1 "\t" $2;total=$2+$4} END {printf "TOTAL\t%.0f\n",total}' "$out/baseline/intermediates.tsv" > "$out/baseline/layout-actual.tsv"
cmp "$out/baseline/layout-expected.tsv" "$out/baseline/layout-actual.tsv"
test "$(wc -l < "$out/baseline/operations.tsv")" = 437
test "$(awk '$1=="op"{n++}END{print n}' "$out/baseline/decode.oev")" = 52
test "$(awk '$1=="resource" && $3=="kind=capacity"{n++}END{print n}' "$out/baseline/decode.oev")" = 1
# Assert deterministic output and a named-source mismatch refusal.
cp "$out/baseline/intermediates.tsv" "$out/baseline/intermediates.first"
sh research/locality_l491/baseline.sh
cmp "$out/baseline/intermediates.first" "$out/baseline/intermediates.tsv"
printf 'source_mutant' >> "$out/baseline/source/omega_gpu_decode_graph.c"
if (cd "$out/baseline/source"; sha256sum -c "$root/research/locality_l491/source-lock.sha256") > "$out/baseline/mismatch.log" 2>&1; then exit 1; fi
grep -q '^omega_gpu_decode_graph.c: FAILED$' "$out/baseline/mismatch.log"
# Restore from the pinned git object, never weaken the source lock.
sh research/locality_l491/baseline.sh
printf 'baseline: 18 exact offsets+total match native source; 436 launches; 52 OEV1 macro ops; one capacity; deterministic; changed source refuses PASS\n'
