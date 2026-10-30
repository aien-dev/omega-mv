#!/bin/sh
# seed/tilelab/gate.sh -- host gate for the native .om tile planner and streaming-softmax algebra.
# Run from the repo root after seed/omega-from-seed.sh:  sh seed/tilelab/gate.sh
# Compiles seed/tilelab/tilelab.om with seed/out/omc (sha checked against seed/out/omc.sha256), runs every
# case in seed/tilelab/cases.txt, and requires stdout and exit status to equal seed/tilelab/expected.txt
# byte for byte. expected.txt is the program's frozen output (a regression pin); the planner picks, bytes,
# costs, direct value and softmax error figures in it were reproduced independently in Wolfram Language
# (seed/tilelab/WOLFRAM.md lists each value and query; the rest is pinned, not verified). Then checks that each softmax mutant fails for its own reason:
# mut 1 only on one_err, mut 2 on seq_err and tree_err, mut 3 only on tree_err.
# Prints PASS tilelab and exits 0, or FAIL and exits 1.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
D=seed/tilelab
B=build/seed/tilelab
mkdir -p "$B"
bad() { printf 'FAIL tilelab: %s\n' "$*" >&2; exit 1; }

want=$(cut -c1-64 seed/out/omc.sha256)
have=$(sha256sum < seed/out/omc | cut -c1-64)
[ "$want" = "$have" ] || bad "seed/out/omc is $have, omc.sha256 says $want"
seed/out/omc < "$D/tilelab.om" > "$B/tilelab" || bad "omc refused tilelab.om"
chmod +x "$B/tilelab"
printf 'omc %s\ntilelab %s\n' "$have" "$(sha256sum < "$B/tilelab" | cut -c1-64)"

: > "$B/observed.txt"
while IFS= read -r args; do
  case "$args" in ''|'#'*) continue ;; esac
  # word splitting of $args is intended: each line is one argument vector
  # shellcheck disable=SC2086
  out=$("$B/tilelab" $args | grep -v '^cfg ' || true)
  # shellcheck disable=SC2086
  "$B/tilelab" $args > /dev/null && rc=0 || rc=$?
  printf '$ tilelab %s\n%s\nrc=%s\n' "$args" "$out" "$rc" >> "$B/observed.txt"
done < "$D/cases.txt"
# the picked cfg row with its byte model, per planner shape (checked against Wolfram)
for s in "1024 1024 1024" "4096 4096 4096" "2048 8192 2048" "100 100 100" "8192 8192 8192"; do
  # shellcheck disable=SC2086
  p=$("$B/tilelab" 1 $s | sed -n 's/^pick cfg \(rt=[0-9]* ct=[0-9]* wm=[0-9]* wn=[0-9]* band=[0-9]*\)$/\1 /p')
  # shellcheck disable=SC2086
  "$B/tilelab" 1 $s | grep "^cfg $p" | head -n 1 | sed "s/^/row $s: /" >> "$B/observed.txt"
done
cmp -s "$B/observed.txt" "$D/expected.txt" || { diff "$D/expected.txt" "$B/observed.txt" >&2 || true; bad "output differs from expected.txt"; }

col() { "$B/tilelab" 2 "$1" | sed -n "s/.* $2=\([0-9]*\).*/\1/p"; }
tol=2000000
over() { [ "$(col "$1" "$2")" -gt "$tol" ]; }
over 0 seq_err_ppt && bad "mut 0 seq_err over tolerance"
over 0 tree_err_ppt && bad "mut 0 tree_err over tolerance"
over 1 seq_err_ppt && bad "mut 1 fails for a reason other than tile-local normalisation"
over 1 one_err_ppt || bad "mut 1 one_err not over tolerance"
over 2 seq_err_ppt || bad "mut 2 seq_err not over tolerance"
over 2 tree_err_ppt || bad "mut 2 tree_err not over tolerance"
over 3 seq_err_ppt && bad "mut 3 fails for a reason other than tree order"
over 3 tree_err_ppt || bad "mut 3 tree_err not over tolerance"
# All representable unsigned budgets remain valid, including the former failure sentinel.
"$B/tilelab" 1 8 8 8 18446744073709551615 > "$B/max-regs.txt" || bad "maximum ulong REGS refused"
grep -q '^pick cfg ' "$B/max-regs.txt" || bad "maximum ulong REGS did not produce a plan"
if "$B/tilelab" 1 8 8 8 "" > "$B/empty-regs.txt"; then rc=0; else rc=$?; fi
[ "$rc" -eq 3 ] || bad "empty REGS returned $rc, wanted usage refusal 3"
printf 'tilelab: usage: tilelab 1 M N K [REGS] | tilelab 2 [MUT]\n' > "$B/usage.txt"
cmp -s "$B/empty-regs.txt" "$B/usage.txt" || bad "empty REGS emitted a plan or unexpected diagnostic"

# The .om fixture removes one guard at a time; both controls must actually compile and
# produce a successful plan for the otherwise-refused audit input (not merely fail).
seed/out/omc < "$D/parser_mutant.om" > "$B/parser-mutant" || bad "omc refused parser_mutant.om"
chmod +x "$B/parser-mutant"
for mut in 1 2; do
  "$B/parser-mutant" "$mut" < "$D/tilelab.om" > "$B/mutant-$mut.om" || bad "could not remove parser guard $mut"
  seed/out/omc < "$B/mutant-$mut.om" > "$B/mutant-$mut" || bad "omc refused parser mutant $mut"
  chmod +x "$B/mutant-$mut"
  if [ "$mut" -eq 1 ]; then
    "$B/mutant-$mut" 1 18446744073709551617 8 8 > "$B/mutant-$mut.txt" || bad "overflow control did not reach the planner"
  else
    "$B/mutant-$mut" 1 1024 1024 1024 extra > "$B/mutant-$mut.txt" || bad "REGS control did not reach the planner"
  fi
  grep -q '^pick cfg ' "$B/mutant-$mut.txt" || bad "parser control $mut did not restore invalid-input acceptance"
done
printf 'PASS tilelab %s cases, 3/3 softmax mutants fail for the intended reason, 2/2 parser guards challenged\n' "$(grep -c '^\$ ' "$B/observed.txt")"
