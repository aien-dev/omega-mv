#!/usr/bin/env bash
# run.sh: build the geometry solver, prove it, and write every table L323 reports. No chip, no timing: pure arithmetic.
# Output: evidence/geometry/<stamp>/ with selftest, edges, parts, rank (fast) == brute (every m), sweeps, configs, SHA256SUMS.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out="$here/${1:-run-$(date -u +%Y%m%dT%H%MZ)}"
mkdir -p "$out"
bin=$(mktemp /tmp/geometry-XXXXXX)
trap 'rm -f "$bin"' EXIT
gcc -std=c11 -O2 -Wall -Wextra -Werror -o "$bin" "$root/tools/geometry/geometry.c" -lm
parts="$here/parts-35b.tsv"
g() { "$bin" "$parts" "$@"; }
{ echo "omega $(git -C "$root" rev-parse HEAD)"; sha256sum "$root/tools/geometry/geometry.c" "$parts"; date -u +%FT%TZ; } > "$out/stamp.txt"
g selftest > "$out/selftest.txt"
g edges > "$out/edges.tsv"
g parts > "$out/parts.tsv"
g rank > "$out/rank.tsv"
g brute > "$out/rank-brute.tsv"
cmp "$out/rank.tsv" "$out/rank-brute.tsv" && echo "rank == brute force: identical" > "$out/exact.txt"
# sweeps over the UNKNOWN edges, one variable at a time from the default
while read -r name args; do
	# shellcheck disable=SC2086
	g rank $args > "$out/sweep-$name.tsv"
done <<'SWEEPS'
x0 x_us=0
x10 x_us=10
x20 x_us=20
step0 step_us=0
step30 step_us=30
prefetch16 prefetch_mb=16
l2hold22 l2_hold_mb=22
road2-100 road2_gbs=100
road2-300 road2_gbs=300
cap49 cap_mb=49
bw245 bwr_gbs=245.1
ctx32k ctx=32768
gbe10 gbe_rtt_us=10
gbe100 gbe_rtt_us=100
best-case x_us=1 step_us=30 prefetch_mb=16 l2_hold_mb=22 road2_gbs=100
SWEEPS
for f in 0 1 2 3; do for n in 1 8 32; do g config format=$f N=$n > "$out/config-f$f-n$n.toml"; done; done
(cd "$out" && sha256sum ./*.tsv ./*.txt ./*.toml > SHA256SUMS)
grep -E "PASS|FAIL" "$out/selftest.txt"
cat "$out/exact.txt"
echo "$out"
