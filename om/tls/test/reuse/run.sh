#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# run.sh: OMEGA-CRYPTO-REUSE-1 gates 1 and 2 (issue #639). Orchestration only; the checks are
# in driver.om. Usage: sh om/tls/test/reuse/run.sh OMC   (run from the repository root)
# Builds om2 + om/tls/chacha.om + om/tls/aesgcm.om + om/tls/gcmsiv.om + driver.om, expects all 27
# checks to pass on vectors.txt, then requires every mutant to be caught: the mutant must build
# (rc 0, empty stderr), exit 1, report F > 0 failed checks and print exactly F "MISMATCH check" or
# "BAD input" lines. A mutant that fails to build, traps or crashes is INVALID and fails the gate.
set -u
OMC=${1:?usage: sh om/tls/test/reuse/run.sh OMC}
D=om/tls/test/reuse
B=build/tls/reuse
mkdir -p "$B"
fail() { echo "REUSE FAIL: $*"; exit 1; }

build() { # $1 driver source, $2 output
  { printf 'om2\n'; cat om/tls/chacha.om om/tls/aesgcm.om om/tls/gcmsiv.om "$1"; } > "$2.unit"
  "$OMC" < "$2.unit" > "$2" 2> "$2.err"; rc=$?
  [ "$rc" -eq 0 ] && [ ! -s "$2.err" ] || return 1
  chmod +x "$2"
}

build "$D/driver.om" "$B/driver" || fail "driver build: $(cat "$B/driver.err")"
out=$("$B/driver" < "$D/vectors.txt"); rc=$?
[ "$rc" -eq 0 ] || fail "base exit $rc: $out"
[ "$out" = "reuse driver: 27 checks, 0 failed" ] || fail "base output: $out"

caught() { # $1 binary, $2 vectors, $3 name, $4 expected failed count or "any"
  o=$("$1" < "$2"); r=$?
  [ "$r" -eq 1 ] || fail "mutant $3: exit $r (want 1, INVALID if trap/crash): $o"
  f=$(printf '%s\n' "$o" | sed -n 's/^reuse driver: 27 checks, \([0-9]*\) failed$/\1/p')
  [ -n "$f" ] && [ "$f" -gt 0 ] || fail "mutant $3: no failure summary: $o"
  m=$(printf '%s\n' "$o" | grep -c -e '^MISMATCH check ' -e '^BAD input$')
  [ "$m" -eq "$f" ] || fail "mutant $3: $m mismatch lines for $f failed"
  [ "$4" = any ] || [ "$f" -eq "$4" ] || fail "mutant $3: $f failed (want $4)"
  echo "caught $3: $f failed"
}

n=0
mut() { # $1 name, $2 site (must occur exactly once), $3 replacement
  c=$(grep -cxF "$2" "$D/driver.om")
  [ "$c" -eq 1 ] || fail "mutant $1: site found $c times"
  awk -v s="$2" -v r="$3" '$0 == s { print r; next } { print }' "$D/driver.om" > "$B/m.om"
  build "$B/m.om" "$B/m" || fail "mutant $1 INVALID: build failed: $(cat "$B/m.err")"
  caught "$B/m" "$D/vectors.txt" "$1" any
  n=$((n + 1))
}

mut tags-in-detector 'dchk kreuse w OC1 OC2 OP1 OP2 RLEN add b 2' 'dchk kreuse w OC1 OC2 OP1 OP2 SLEN add b 2'
mut detector-always-reuse 'ret not ne acc 0' 'ret true'
mut nonce-not-distinct 'put w last xor get w last 1' 'put w last xor get w last 0'
mut tamper-not-applied 'put w add OT 5 xor get w add OT 5 1' 'put w add OT 5 xor get w add OT 5 0'
mut siv-as-other-mode 'r1 = dseal 2 w ON1 nl OC1' 'r1 = dseal 1 w ON1 nl OC1'

# expectation mutant: one digit of X wrong -> checks 1, 5, 13 fail and nothing else
sed 's/^X 6/X 7/' "$D/vectors.txt" > "$B/vx.txt"
cmp -s "$B/vx.txt" "$D/vectors.txt" && fail "vector mutant not applied"
caught "$B/driver" "$B/vx.txt" wrong-expected-xor 3
n=$((n + 1))

echo "REUSE PASS checks=27 mutants=$n/$n"
