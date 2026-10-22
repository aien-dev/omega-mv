#!/bin/bash
set -eu
root=$(git rev-parse --show-toplevel)
d=${1:?output dir}
mkdir -p "$d"
s=$root/bench/m1/block3_receipt_csv.sh
base=$root/evidence/L511-M1-BLOCK3/receipt.json
if ! test -f "$base"; then base=$(find "$root/evidence/L511-M1-BLOCK3" -name '*.json' | head -1); fi
probe() {
 name=$1; expr=$2
 jq "$expr" "$base" > "$d/$name.json"
 set +e
 "$s" "$d/$name.json" "$d/$name.csv" 2> "$d/$name.err"
 rc=$?
 set -e
 printf '%s exit=%s\n' "$name" "$rc"
}
"$root/tests/m1/block3_csv_test.sh"
"$s" "$base" "$d/baseline.csv"
cmp "$d/baseline.csv" "$root/evidence/L511-M1-BLOCK3/block3.csv"
probe fail '.failures=1'
probe unbound '.candidate_bound=false'
probe negative '.workloads.reverse_tower_6.first_use.median_cognition_cpu_ns=-1'
probe numeric-string '.workloads.reverse_tower_6.first_use.median_end_to_end_ns="2"'
probe overflow '.workloads.reverse_tower_6.first_use.median_end_to_end_ns=1e999'
probe fractional '.workloads.reverse_tower_6.first_use.median_end_to_end_ns=0.5'
probe checks-string '.checks="invalid"'
probe formula '.host.machine="=1+1"'
probe missing-machine 'del(.host.machine)'
probe false-binding '.candidate_commit="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" | .run_commit=.candidate_commit | .test_binary_sha256="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"'
jq '.failures=1 | .candidate_bound=false | .workloads.reverse_tower_6.reuse_succeeded=0' "$base" > "$d/stream.json"
cat "$base" >> "$d/stream.json"
set +e
"$s" "$d/stream.json" "$d/stream.csv" 2> "$d/stream.err"
rc=$?
set -e
printf 'invalid-then-valid-stream exit=%s rows=%s\n' "$rc" "$(wc -l < "$d/stream.csv")"
mkdir -p "$d/space path"
LC_ALL=C "$s" "$base" "$d/space path/result.csv"
LC_ALL=C.utf8 "$s" "$base" "$d/utf8.csv"
cmp "$d/utf8.csv" "$d/baseline.csv"
"$s" "$base" "$d/space path"
printf 'directory-output exit=0 files=%s\n' "$(find "$d/space path" -type f | wc -l)"
