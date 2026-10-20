#!/usr/bin/env bash
# tools/omegese/tokens_structs.sh <tokcount> <tokenizer.json> [<table.md>] -- the struct spelling measurement
# (docs/one_layer/OMEGESE.md section 5.1), by the phase 1 method (section 5): the Qwen3 tokenizer, offline,
# counts the exact text of each of the six struct twins in each spelling: OSC text (the .osc twin
# without its // comment lines), Omegese A (tests/one_layer/omegese/structs/spelling_a: new S, then a
# put per field), and Omegese B (the twin itself: new S v1 .. vn).
# <tokcount> is `tokcount <tokenizer.json> count <file>...` (one count per line, in order).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
[ $# -ge 2 ] && [ $# -le 3 ] || { echo "usage: tokens_structs.sh <tokcount> <tokenizer.json> [<table.md>]" >&2; exit 2; }
TC="$1" TK="$2" OUT="${3:-/dev/stdout}"
T="$(mktemp -d "${TMPDIR:-/tmp}/omegese-tokens.XXXXXXXX")"; trap 'rm -rf "$T"' EXIT
D="$ROOT/tests/one_layer/omegese/structs"
count() { "$TC" "$TK" count "$1" | awk '{ print $1 }'; }
sa=0 sb=0 st=0
{
  echo "| twin | OSC text | Omegese A | Omegese B |"
  echo "|---|---|---|---|"
  for o in "$D"/t*.omegese; do
    id="$(basename "$o" .omegese)"
    grep -v '^//' "${o%.omegese}.osc" > "$T/$id.osc"
    t="$(count "$T/$id.osc")" a="$(count "$D/spelling_a/$id.omegese")" b="$(count "$o")"
    st=$((st + t)) sa=$((sa + a)) sb=$((sb + b))
    echo "| $id | $t | $a | $b |"
  done
  echo "| **total** | **$st** | **$sa** | **$sb** |"
} > "$OUT"
echo "tokens: OSC text $st, Omegese A $sa, Omegese B $sb" >&2
