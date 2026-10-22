#!/bin/sh
# Offline conversion only: matching self-reported pins are not source attestation.
set -eu
if [ "$#" -ne 2 ]; then echo 'usage: block3_receipt_csv.sh RECEIPT.json OUTPUT.csv' >&2; exit 2; fi
receipt=$1
output=$2
file_target() {
 [ ! -L "$output" ] && { [ ! -e "$output" ] || [ -f "$output" ]; } &&
 [ -n "$output" ] && [ "${output%/}" = "$output" ]
}
refuse() { echo 'block3: refused invalid receipt or non-file output target' >&2; exit 1; }
file_target || refuse
# Capture once. Validation, CSV fields and raw-byte SHA all use this private copy.
work=$(mktemp -d)
tmp=
trap 'rm -f "$work/receipt"; rmdir "$work"; [ -z "$tmp" ] || rm -f "$tmp"' EXIT
cat -- "$receipt" > "$work/receipt" || refuse
chmod 400 "$work/receipt"
receipt_sha=$(sha256sum "$work/receipt" | cut -d' ' -f1)
# Same-directory temporary + rename gives atomic publication at exactly OUTPUT.
tmp=$(mktemp "$(dirname -- "$output")/.block3.XXXXXXXX")
if ! jq -s -r --arg sha "$receipt_sha" '
 # jq 1.7 preserves number literals for tostring. Require producer decimal integers
 # before arithmetic can round fractions/underflow into integers. The binary64
 # exact-integer bound is also below the producer uint64 nanosecond limit.
 def uint: if type != "number" then false else
   (tostring | test("^(0|[1-9][0-9]*)$")) and
   . >= 0 and . <= 9007199254740991 and . == floor end;
 def positive_uint: uint and . > 0;
 def machine: if type != "string" then false else
   length >= 1 and length <= 64 and test("^[A-Za-z0-9][A-Za-z0-9_.-]*$") and
   (contains("\n") | not) end;
 def hex($n): if type != "string" then false else
   length == $n and test("^[0-9a-f]+$") end;
 if (9007199254740990.5 | tostring) != "9007199254740990.5"
 then error("literal-preserving jq required") else . end |
 if length != 1 then error("exactly one receipt required") else .[0] end |
 if type != "object" then error("receipt must be an object") else . end |
 if (
 .schema == "OMEGA_PLAN_REUSE_V1" and .candidate_bound == true and .tree_dirty == false and
 (.candidate_commit | hex(40)) and .candidate_commit == .run_commit and
 (.failures | uint) and .failures == 0 and (.checks | positive_uint) and
 (.host.machine | machine) and (.test_binary_sha256 | hex(64)) and
 (. as $root | all(["reverse_tower_6", "merge_two_towers_3_3"][]; . as $name |
   $root.workloads[$name] as $w | $w != null and
   ($w.instances | uint) and $w.instances == 40 and
   ($w.first_use.n | uint) and $w.first_use.n == 40 and
   ($w.reuse.n | uint) and $w.reuse.n == 39 and
   ($w.reuse_accepted | uint) and $w.reuse_accepted == 39 and
   ($w.reuse_succeeded | uint) and $w.reuse_succeeded == 39 and
   ($w.outcome_equal_to_first_use | uint) and $w.outcome_equal_to_first_use == 40 and
   ($w.evidence_equal_to_first_use | uint) and $w.evidence_equal_to_first_use == 40 and
   ($w.first_use.median_cognition_cpu_ns | uint) and
   ($w.reuse.median_cognition_cpu_ns | uint) and
   ($w.first_use.median_end_to_end_ns | positive_uint) and
   ($w.reuse.median_end_to_end_ns | positive_uint)))
 ) then . else error("invalid, unbound, failing, or incomplete receipt") end |
 "workload,first_n,reuse_n,first_cpu_ns,reuse_cpu_ns,first_e2e_ns,reuse_e2e_ns,outcome_equal,evidence_equal,retained_bytes,lookup_ns,wall_energy_j,run_commit,binary_sha256,machine,receipt_sha256",
 (["reverse_tower_6", "merge_two_towers_3_3"][] as $name |
 . as $r | .workloads[$name] as $w |
 [$name,$w.first_use.n,$w.reuse.n,$w.first_use.median_cognition_cpu_ns,
  $w.reuse.median_cognition_cpu_ns,$w.first_use.median_end_to_end_ns,
  $w.reuse.median_end_to_end_ns,$w.outcome_equal_to_first_use,
  $w.evidence_equal_to_first_use,"UNKNOWN","UNKNOWN","UNMEASURED",
  $r.run_commit,$r.test_binary_sha256,$r.host.machine,$sha] | @csv)
' "$work/receipt" > "$tmp"; then refuse; fi
file_target || refuse
mv -fT -- "$tmp" "$output"
