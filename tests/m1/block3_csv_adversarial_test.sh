#!/bin/bash
# Offline converter boundary tests. Optional output directory retains every fixture/result.
set -euo pipefail
script=${BLOCK3_CONVERTER:-bench/m1/block3_receipt_csv.sh}
if [[ $# == 0 ]]; then d=$(mktemp -d); trap 'rm -r "$d"' EXIT; else d=$1; mkdir -p "$d"; fi
d=$(realpath "$d")
base=evidence/L511-M1-BLOCK3/plan-reuse-receipt.json
cp "$base" "$d/genuine.json"
failed=0; checked=0
refuse() {
 local name=$1 rc=0
 printf 'existing output\n' > "$d/out.csv"
 "$script" "$d/$name.json" "$d/out.csv" > "$d/$name.stdout" 2> "$d/$name.stderr" || rc=$?
 checked=$((checked+1))
 if [[ $rc == 1 ]] && [[ $(cat "$d/out.csv") == 'existing output' ]]; then
  echo "PASS refusal $name exit=$rc output preserved"
 else echo "FAIL refusal $name exit=$rc"; failed=$((failed+1)); fi
}
mutate() { jq "$2" "$d/genuine.json" > "$d/$1.json"; refuse "$1"; }
mutate failing '.failures=1'
cat "$d/failing.json" "$d/genuine.json" > "$d/invalid-valid.json"; refuse invalid-valid
cat "$d/genuine.json" "$d/failing.json" > "$d/valid-invalid.json"; refuse valid-invalid
cat "$d/genuine.json" "$d/genuine.json" > "$d/multi-valid.json"; refuse multi-valid
: > "$d/empty.json"; refuse empty
printf '[' > "$d/malformed.json"; refuse malformed
jq -s '.' "$d/genuine.json" > "$d/array.json"; refuse array
printf 'null\n' > "$d/null.json"; refuse null
for item in '"=1+1"' '"+1"' '"-1"' '"@x"' '"\tx"' '"x\ny"' '"x\"y"' '"x,y"' 'null' '[]' '""' '42' 'true'; do
 name=machine-$checked; mutate "$name" ".host.machine=$item"
done
mutate machine-missing 'del(.host.machine)'
mutate machine-long '.host.machine=("a"*65)'
for field in '.checks' '.failures' '.workloads.reverse_tower_6.instances' '.workloads.reverse_tower_6.first_use.n' '.workloads.reverse_tower_6.reuse.n' '.workloads.reverse_tower_6.reuse_accepted' '.workloads.reverse_tower_6.reuse_succeeded' '.workloads.reverse_tower_6.outcome_equal_to_first_use' '.workloads.reverse_tower_6.evidence_equal_to_first_use' '.workloads.reverse_tower_6.first_use.median_cognition_cpu_ns' '.workloads.reverse_tower_6.reuse.median_cognition_cpu_ns' '.workloads.reverse_tower_6.first_use.median_end_to_end_ns' '.workloads.reverse_tower_6.reuse.median_end_to_end_ns'; do
 for value in '"invalid"' 'true' 'null' '-1' '0.5' '1e999' '9007199254740992' '9.007199254740992e15'; do
  name=numeric-$checked; mutate "$name" "$field=$value"
 done
done
mutate checks-zero '.checks=0'
mutate e2e-zero '.workloads.reverse_tower_6.first_use.median_end_to_end_ns=0'
mutate rounded-fraction '.workloads.reverse_tower_6.first_use.median_cognition_cpu_ns=9007199254740990.5'
mutate underflow-fraction '.workloads.reverse_tower_6.first_use.median_cognition_cpu_ns=1e-999'
# Known genuine receipt and raw-byte provenance must remain exactly reproducible.
"$script" "$d/genuine.json" "$d/genuine.csv"
cmp "$d/genuine.csv" evidence/L511-M1-BLOCK3/block3.csv
for machine in aarch64 x86_64; do
 jq --arg m "$machine" '.host.machine=$m' "$d/genuine.json" > "$d/$machine.json"
 "$script" "$d/$machine.json" "$d/$machine.csv"
 sha=$(sha256sum "$d/$machine.json" | cut -d' ' -f1)
 rg -q ",\"$machine\",\"$sha\"$" "$d/$machine.csv"
done
jq '.checks=9007199254740991 | .workloads.reverse_tower_6.first_use.median_cognition_cpu_ns=0 | .workloads.reverse_tower_6.reuse.median_cognition_cpu_ns=9007199254740991' "$d/genuine.json" > "$d/boundary.json"
"$script" "$d/boundary.json" "$d/boundary.csv"
# Paths are file targets, never implicit directories or device replacements.
mkdir -p "$d/directory"
mkfifo "$d/fifo"
ln -s genuine.json "$d/link"
targets=("$d/directory" "$d/fifo" "$d/link")
# The vulnerable original would replace /dev/null; exercise only private paths in RED.
if [[ ${BLOCK3_RED_ONLY:-0} != 1 ]]; then targets+=(/dev/null); fi
for target in "${targets[@]}"; do
 before=$(stat -c '%F:%i:%s' "$target")
 rc=0; "$script" "$d/genuine.json" "$target" > "$d/path.stdout" 2> "$d/path.stderr" || rc=$?
 after=$(stat -c '%F:%i:%s' "$target")
 checked=$((checked+1))
 if [[ $rc == 1 && $before == "$after" ]]; then echo "PASS path refusal $target"; else echo "FAIL path refusal $target exit=$rc"; failed=$((failed+1)); fi
done
[[ -z $(find "$d/directory" -mindepth 1 -print -quit) ]] || { echo 'FAIL directory polluted'; failed=$((failed+1)); }
# Change original pathname at first jq call. All subsequent work must use captured bytes.
mkdir -p "$d/bin"
real_jq=$(command -v jq)
cat > "$d/bin/jq" <<'WRAP'
#!/bin/sh
if [ ! -f "$SNAP_MARK" ]; then
 : > "$SNAP_MARK"
 cp "$SNAP_REPLACEMENT" "$SNAP_ORIGINAL"
fi
exec "$REAL_JQ" "$@"
WRAP
chmod +x "$d/bin/jq"
cp "$d/genuine.json" "$d/snapshot.json"
cp "$d/genuine.csv" "$d/expected.csv"
rc=0
PATH="$d/bin:$PATH" REAL_JQ="$real_jq" SNAP_MARK="$d/mark" SNAP_ORIGINAL="$d/snapshot.json" SNAP_REPLACEMENT="$d/failing.json" "$script" "$d/snapshot.json" "$d/snapshot.csv" > "$d/snapshot.stdout" 2> "$d/snapshot.stderr" || rc=$?
checked=$((checked+1))
if [[ $rc == 0 ]] && cmp -s "$d/expected.csv" "$d/snapshot.csv"; then echo 'PASS immutable snapshot'; else echo "FAIL immutable snapshot exit=$rc"; failed=$((failed+1)); fi
printf 'block3 adversarial: %s assertions, %s failures\n' "$checked" "$failed"
[[ $failed == 0 ]]
