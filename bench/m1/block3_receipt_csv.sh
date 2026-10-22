#!/bin/sh
# Export the existing plan-reuse qualification receipt without inventing missing measurements.
set -eu
if [ "$#" -ne 2 ]; then echo 'usage: block3_receipt_csv.sh RECEIPT.json OUTPUT.csv' >&2; exit 2; fi
receipt=$1
output=$2
if ! jq -e '
 .schema == "OMEGA_PLAN_REUSE_V1" and .candidate_bound == true and .tree_dirty == false and
 (.candidate_commit | test("^[0-9a-f]{40}$")) and .candidate_commit == .run_commit and
 .failures == 0 and (.checks > 0) and
 (.test_binary_sha256 | test("^[0-9a-f]{64}$")) and
 . as $root | all(["reverse_tower_6", "merge_two_towers_3_3"][]; . as $name |
   $root.workloads[$name] as $w | $w != null and $w.instances == 40 and
   $w.first_use.n == 40 and $w.reuse.n == 39 and
   $w.reuse_accepted == 39 and $w.reuse_succeeded == 39 and
   $w.outcome_equal_to_first_use == 40 and $w.evidence_equal_to_first_use == 40 and
   ($w.first_use.median_cognition_cpu_ns | type == "number" and . >= 0) and
   ($w.reuse.median_cognition_cpu_ns | type == "number" and . >= 0) and
   ($w.first_use.median_end_to_end_ns | type == "number" and . > 0) and
   ($w.reuse.median_end_to_end_ns | type == "number" and . > 0))
' "$receipt" >/dev/null; then
 echo 'block3: refused invalid, unbound, failing, or incomplete plan-reuse receipt' >&2
 exit 1
fi
receipt_sha=$(sha256sum "$receipt" | cut -d' ' -f1)
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
printf '%s\n' 'workload,first_n,reuse_n,first_cpu_ns,reuse_cpu_ns,first_e2e_ns,reuse_e2e_ns,outcome_equal,evidence_equal,retained_bytes,lookup_ns,wall_energy_j,run_commit,binary_sha256,machine,receipt_sha256' > "$tmp"
jq -r --arg sha "$receipt_sha" '
 ["reverse_tower_6", "merge_two_towers_3_3"][] as $name |
 . as $r | .workloads[$name] as $w |
 [$name,$w.first_use.n,$w.reuse.n,$w.first_use.median_cognition_cpu_ns,
  $w.reuse.median_cognition_cpu_ns,$w.first_use.median_end_to_end_ns,
  $w.reuse.median_end_to_end_ns,$w.outcome_equal_to_first_use,
  $w.evidence_equal_to_first_use,"UNKNOWN","UNKNOWN","UNMEASURED",
  $r.run_commit,$r.test_binary_sha256,$r.host.machine,$sha] | @csv
' "$receipt" >> "$tmp"
mv "$tmp" "$output"
