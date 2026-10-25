#!/bin/sh
# Parity of the Omega BLAKE3 (src/omega_blake3.c adapter + om/blake3/b3unit.om in src/omega_om_units.inc)
# against the C it replaced: src/omega_blake3.c at 633a1735 (sha256 c89ef819...0f21). The 35 official
# vectors (tests/resolve/blake3_vectors.txt) and 1000 inputs (boundaries, random to 3 MiB) must agree.
# Then the two mutants that make test-resolve used to apply to the C (B3M:merge, B3M:root) are
# applied to b3unit.om; each rebuilt library must fail the official vectors.
#   OMEGA_OMC, OMEGA_BRIDGE as for om/units.sh (exit 3 NOT_RUN without them)
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
ORACLE=633a17353ba9a62761c9ccb83416d0dbd1689cfe
V=$ROOT/tests/resolve/blake3_vectors.txt
git -C "$ROOT" show "$ORACLE:src/omega_blake3.c" > "$T/c.c"
[ "$(sha256sum < "$T/c.c" | cut -c1-64)" = c89ef819cb5fd4a77a2efe89cfb8d319d1fd2d07e14f02e4ffe263db334d0f21 ]
sed 's/omega_blake3_hash/orc_blake3_hash/' "$T/c.c" > "$T/oracle.c"
CF="-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2"
build() { # build <src dir> <out>
  cc $CF -I"$1" -c "$1/omega_blake3.c" -o "$T/b3.o"
  cc $CF -I"$1" -c "$1/sha256.c" -o "$T/sha.o"
  cc -O2 -I"$ROOT/src" -c "$T/oracle.c" -o "$T/oracle.o"
  cc -O2 "$(dirname "$0")/par.c" "$T/b3.o" "$T/sha.o" "$T/oracle.o" -o "$2" -pthread
}
build "$ROOT/src" "$T/par"
out=$("$T/par" "$V" | tail -1); echo "$out"
[ "$out" = "vectors=35 vecfail=0 cases=1000 diff=0" ] || { echo "BLAKE3-PARITY FAIL"; exit 1; }
# The mutants are part of the verdict: without the pinned tools this is NOT_RUN (exit 3), never PASS.
[ -n "${OMEGA_OMC:-}" ] && [ -n "${OMEGA_BRIDGE:-}" ] || { echo "BLAKE3-PARITY NOT_RUN: the mutants need OMEGA_OMC and OMEGA_BRIDGE"; exit 3; }
mutant() { # mutant <name> <exact line> <replacement> <vectors that must differ>
  rm -rf "$T/m"; mkdir -p "$T/m/om"; cp -r "$ROOT/om/." "$T/m/om/"; cp -r "$ROOT/src" "$T/m/src"
  python3 - "$T/m/om/blake3/b3unit.om" "$2" "$3" <<'PY'
import sys
p, old, new = sys.argv[1:]
s = open(p).read()
assert s.count(old + "\n") == 1, "mutant line not found once"
open(p, "w").write(s.replace(old + "\n", new + "\n"))
PY
  sh "$T/m/om/units.sh" > /dev/null
  build "$T/m/src" "$T/mpar"
  # Killed means: the driver ran to the end and reported exactly the expected official-vector failures
  # (exit 1, summary line present). A crash, abort or other exit is not a kill.
  rc=0; "$T/mpar" "$V" > "$T/m.out" 2>&1 || rc=$?
  got=$(grep -c 'VEC FAIL' "$T/m.out")
  if [ "$rc" -ne 1 ] || ! grep -q "^vectors=35 vecfail=$4 " "$T/m.out" || [ "$got" -ne "$4" ]; then
    echo "mutant $1 NOT killed as required (exit $rc, $got vector failures, want exit 1 and $4)"; tail -3 "$T/m.out"; exit 1
  fi
  echo "mutant $1 killed (exit 1, $got of 35 vectors differ)"
}
mutant b3-merge 'while eq and tot 1 0 bound 64' 'while and eq and tot 1 0 gt get st 440 1 bound 64' 16
mutant b3-root 'put w 74 or get w 74 8' 'put w 74 or get w 74 0' 35
echo "BLAKE3-PARITY PASS"
