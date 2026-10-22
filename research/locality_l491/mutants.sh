#!/bin/sh
# Isolated behavioral mutants of the Omega implementation, not source-only checks.
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
compiler="$out/seed-src/seed/out/omc"
mkdir -p "$out/mutants"
mutant() {
 name=$1; expr=$2
 sed "$expr" research/locality_l491/locality.om > "$out/mutants/$name.om"
 ! cmp -s research/locality_l491/locality.om "$out/mutants/$name.om"
 "$compiler" < "$out/mutants/$name.om" > "$out/mutants/$name"
 chmod +x "$out/mutants/$name"
 set +e
 "$out/oracle" "$out/mutants/$name" > "$out/mutants/$name.log" 2>&1
 rc=$?
 set -e
 test "$rc" = 1
 grep -q 'fail=[1-9]' "$out/mutants/$name.log"
 printf 'KILLED %s: ' "$name"; tail -1 "$out/mutants/$name.log"
}
mutant predecessor 's/if eq get fast j 0/if eq get fast j 2/'
mutant capacity 's/if gt u cap/if lt u 0/'
mutant stateful_recompute 's/if and ne get done i 0 eq and flags 2 0/if and ne get done i 0 eq and flags 2 8/'
mutant output_boundary 's/if eq get slow i 0/if eq get slow i 2/'
mutant traffic 's/put met 0 add get met 0 sz/put met 0 add get met 0 0/'
mutant stale_identity 's/for i ulong 13/for i ulong 12/'
mutant missing_identity 's/if eq get q add i 1 0/if eq get q add i 1 18446744073709551615/'
mutant recurrence_value 's/h = add h add i 1/h = add h add i 2/'
mutant retained_capacity 's/peak = add mul n 16 8/peak = add mul n 8 8/'
mutant recurrence_traffic 's/traffic = mul mul n depth 24/traffic = mul n 24/'
printf 'L491 mutants: 10 killed, 0 survived\n'
