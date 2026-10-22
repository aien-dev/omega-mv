#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
case "$(uname -m)" in aarch64) ;; *) echo 'NOT_RUN: pinned native seed requires AArch64 Linux' >&2;exit 2;; esac
sh research/locality_l491/build.sh
"$out/oracle" "$out/locality"
sh research/locality_l491/baseline.sh
sh research/locality_l491/baseline_test.sh
sh research/locality_l491/oev_test.sh
sh research/locality_l491/training.sh
sh research/locality_l491/mutants.sh
# Strict protocol controls exercise the executable, not just its parser source.
for data in '' '0' '2' '3 0 1 0 64 99 1' '18446744073709551616' '1x' '1 17 24 0 0'; do
 set +e
 printf '%s\n' "$data" | "$out/locality" > "$out/protocol-refusal.out"
 rc=$?
 set -e
 test "$rc" = 20
 test ! -s "$out/protocol-refusal.out"
done
printf 'L491 host batch PASS; GPU/model-quality/performance/energy NOT_RUN\n'
