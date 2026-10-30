#!/bin/sh
# seed/trace/gate.sh -- host gate for the local trace segment (omega-mv #608).
# Run from anywhere:  sh seed/trace/gate.sh
# Compiles segment.om + segment-test.om with seed/out/omc (sha checked against seed/out/omc.sha256), runs
# the test in a fresh scratch directory and requires exit 0 and stdout equal to expected.txt byte for byte.
# The expected values (sizes, offsets, CRC check value, flags) were computed in Wolfram Language; see
# README.md. Then each source mutant (a literal swap of a line that must occur exactly once in segment.om)
# must compile, exit 1, and fail on its own check label:
#   crc     record CRC no longer compared        -> crc-reason FAIL
#   torn    partial tail not reported as torn    -> torn-reason FAIL
#   align   append ignores a misaligned tail     -> torn-append-refused FAIL
#   excl    create opens with O_TRUNC, not O_EXCL -> create-again-refused FAIL
# Prints PASS trace and exits 0, or FAIL and exits 1.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
D=seed/trace
B=build/seed/trace
rm -rf "$B"
mkdir -p "$B"
bad() { printf 'FAIL trace: %s\n' "$*" >&2; exit 1; }

want=$(cut -c1-64 seed/out/omc.sha256)
have=$(sha256sum < seed/out/omc | cut -c1-64)
[ "$want" = "$have" ] || bad "seed/out/omc is $have, omc.sha256 says $want"
OMC="$R/seed/out/omc"

# run NAME SRC: compile SRC to $B/NAME/t, run it inside $B/NAME, stdout to $B/NAME/out.txt, sets rc
run() {
  mkdir -p "$B/$1"
  "$OMC" < "$2" > "$B/$1/t" || bad "omc refused $2"
  chmod 700 "$B/$1/t"
  rc=0
  (cd "$B/$1" && ./t > out.txt) || rc=$?
}

cat "$D/segment.om" "$D/segment-test.om" > "$B/test.om"
run main "$B/test.om"
printf 'omc %s\nsegment-test %s\n' "$have" "$(sha256sum < "$B/main/t" | cut -c1-64)"
[ "$rc" -eq 0 ] || { cat "$B/main/out.txt" >&2; bad "segment-test exited $rc"; }
cmp -s "$B/main/out.txt" "$D/expected.txt" || { diff "$D/expected.txt" "$B/main/out.txt" >&2 || true; bad "output differs from expected.txt"; }

n=0
mutant() { # NAME SITE REPLACEMENT LABEL
  c=$(grep -c -x -F "$2" "$D/segment.om" || true)
  [ "$c" = 1 ] || bad "mutant $1: site found $c times"
  awk -v s="$2" -v r="$3" '$0 == s { print r; next } { print }' "$D/segment.om" > "$B/$1.om"
  cmp -s "$B/$1.om" "$D/segment.om" && bad "mutant $1: source unchanged"
  cat "$B/$1.om" "$D/segment-test.om" > "$B/$1-test.om"
  run "$1" "$B/$1-test.om"
  [ "$rc" -eq 1 ] || bad "mutant $1 exited $rc, need 1"
  grep -q "^$4 [0-9]* FAIL want=" "$B/$1/out.txt" || bad "mutant $1 did not fail $4"
  echo "mutant $1: exit 1, $4 FAIL"
  n=$((n + 1))
}
mutant crc 'if ne want segcrc 60' 'if ne want want' crc-reason
mutant torn 'put sreason 0 1' 'put sreason 0 0' torn-reason
mutant align 'if ne rem r 64 0' 'if ne rem r 1 0' torn-append-refused
mutant excl 'fd = fopen tpath 0 get tlen 0 32961 384' 'fd = fopen tpath 0 get tlen 0 33345 384' create-again-refused
printf 'PASS trace %s checks, %s/4 mutants caught\n' "$(sed -n 's/^segment-test checks=\([0-9]*\) fails=0$/\1/p' "$B/main/out.txt")" "$n"
