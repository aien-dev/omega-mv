#!/usr/bin/env bash
# parts.sh: classify every tensor of a safetensors model into the parts the geometry solver moves.
# Input: tensors TSV (name, dtype, shape, bytes, file) as written by tensors.sh. Output: class<TAB>count<TAB>bytes.
# Classes are per decode word: what is read once per stroke (trunk), per routed expert, per word row (embed), never (vision).
set -euo pipefail
awk -F'\t' '
function cls(n) {
  if (n ~ /visual/) return "vision";
  if (n ~ /^mtp\./) return "mtp";
  if (n ~ /^lm_head/) return "lm_head";
  if (n ~ /embed_tokens/) return "embed";
  if (n ~ /mlp\.experts\./) return "expert";
  if (n ~ /mlp\.gate\.weight/) return "router";
  if (n ~ /shared_expert/) return "shared";
  if (n ~ /linear_attn/) return "linear_attn";
  if (n ~ /self_attn/) return "full_attn";
  return "norms";
}
{ c = cls($1); b[c] += $4; k[c]++; t += $4 }
END { for (c in b) printf "%s\t%d\t%d\n", c, k[c], b[c]; printf "total\t%d\t%d\n", NR, t }' "$1" | sort
