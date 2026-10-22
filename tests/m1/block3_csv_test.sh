#!/bin/sh
set -eu
script=bench/m1/block3_receipt_csv.sh
fixture=$(mktemp)
out=$(mktemp)
target=$(mktemp)
trap 'rm -f "$fixture" "$out" "$target"' EXIT
cat > "$fixture" <<'JSON'
{
 "schema":"OMEGA_PLAN_REUSE_V1","candidate_commit":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
 "candidate_bound":true,"run_commit":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
 "tree_dirty":false,"checks":1304,"failures":0,"test_binary_sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
 "host":{"machine":"aarch64"},"workloads":{
  "reverse_tower_6":{"instances":40,"first_use":{"n":40,"median_cognition_cpu_ns":75000,"median_end_to_end_ns":3000000},"reuse":{"n":39,"median_cognition_cpu_ns":5000,"median_end_to_end_ns":2900000},"reuse_accepted":39,"reuse_succeeded":39,"outcome_equal_to_first_use":40,"evidence_equal_to_first_use":40},
  "merge_two_towers_3_3":{"instances":40,"first_use":{"n":40,"median_cognition_cpu_ns":135000,"median_end_to_end_ns":4100000},"reuse":{"n":39,"median_cognition_cpu_ns":5000,"median_end_to_end_ns":4700000},"reuse_accepted":39,"reuse_succeeded":39,"outcome_equal_to_first_use":40,"evidence_equal_to_first_use":40}
 },"energy":{"measured":false,"why":"no wall sensor"}
}
JSON
$script "$fixture" "$out"
[ "$(wc -l < "$out")" -eq 3 ]
rg -q '"reverse_tower_6",40,39,75000,5000,3000000,2900000' "$out"
rg -q '"merge_two_towers_3_3",40,39,135000,5000,4100000,4700000' "$out"
rg -q ',"UNKNOWN","UNKNOWN","UNMEASURED",' "$out"
for mutation in '.candidate_bound=false' '.tree_dirty=true' '.failures=1' '.workloads.reverse_tower_6.reuse_succeeded=38' '.workloads.reverse_tower_6.first_use.median_end_to_end_ns=null' '.run_commit="different"'; do
  jq "$mutation" "$fixture" > "$out"
  if $script "$out" "$target" 2>/dev/null; then echo "accepted mutant $mutation" >&2; exit 1; fi
done
printf '%s\n' 'block3 CSV contract: PASS (six malformed or unsafe receipts refused)'
