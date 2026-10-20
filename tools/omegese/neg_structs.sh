#!/usr/bin/env bash
# tools/omegese/neg_structs.sh <omegese> [<table.md>] -- the struct refusal gate, runnable on any omegese build
# (docs/one_layer/OMEGESE.md section 3.1). Each case of tests/one_layer/omegese/structs/refusals.txt is a bad
# program and its control (the same program with the defect fixed). A case passes when <omegese> refuses
# the bad program with the expected reason, which names its line, and emits the control. Run on the build
# before structs it must fail (red); on this build it must pass (green). With <table.md>, one row per case.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
[ $# -ge 1 ] && [ $# -le 2 ] || { echo "usage: neg_structs.sh <omegese> [<table.md>]" >&2; exit 2; }
G="$1" OUT="${2:-}"
T="$(mktemp -d "${TMPDIR:-/tmp}/omegese-neg.XXXXXXXX")"; trap 'rm -rf "$T"' EXIT
awk -v d="$T" '
  /^#/ { next }
  /^== / { k++; sub(/^== /, ""); print > (d "/r" k ".want"); f = d "/r" k ".bad"; next }
  /^-- control$/ { f = d "/r" k ".ok"; next }
  { print > f }' "$ROOT/tests/one_layer/omegese/structs/refusals.txt"
n=0 p=0 rows=""
for want in $(ls "$T"/r*.want | sort -V); do
  k="${want%.want}" w="$(cat "$want")" n=$((n + 1))
  out="$("$G" emit "$k.bad" "$T/x.json" 2>&1 || true)"
  ctl=refused; "$G" emit "$k.ok" "$T/x.json" > /dev/null 2>&1 && ctl=accepted
  res=FAIL
  case "$out" in "REFUSED omegese: "*"$w"*) [ "$ctl" = accepted ] && res=PASS ;; esac
  [ "$res" = PASS ] && p=$((p + 1))
  rows+="| $n | $w | ${out//|/\\|} | $ctl | $res |"$'\n'
done
if [ -n "$OUT" ]; then
  {
    echo "| case | expected refusal | omegese said | control | result |"
    echo "|---|---|---|---|---|"
    printf '%s' "$rows"
  } > "$OUT"
fi
echo "struct refusals: $p/$n refused with the expected reason and place, control accepted"
[ "$p" -eq "$n" ] || { echo "neg_structs: FAIL"; exit 1; }
echo "neg_structs: PASS"
