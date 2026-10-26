#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# OM-2 qualification L836 (#748), phaseB/sha-f5d14cb2: source mutants of om/sha256/sha256-om2.om.
# For each NAME.from/NAME.to pair in this folder (and the unmutated CONTROL): the .from bytes must
# occur exactly once in the unit (else INVALID); a scratch copy gets them replaced by the .to bytes,
# keeping the basename sha256-om2.om; it is compiled by the S4 omc (sha256 checked), bound by the
# bridge generator (tools/native), and the ORACLE driver oracle/sha/sha_qual.c is linked against that
# binding. Modes run in order vec, rcpt, part, rand 748 200, bigzero, big; the first FAIL line (status
# 0, wrong digest) is the kill. A TRAP line alone is not a kill; a signal is INVALID.
# Usage (repository root): sh .../run-mutants.sh UNIT OMC OMC_SHA256 SCRATCH [NAME...]
set -u
UNIT=$1; OMC=$2; PIN=$3; S=$4; shift 4
Q=$(cd "$(dirname "$0")/../../.." && pwd)        # seed/qualification/om2
MD=$Q/candidates/sha/mutants
DRV=$Q/oracle/sha/sha_qual.c
VEC=$Q/fixtures/sha256-vectors.txt
ROOT=$(cd "$Q/../../.." && pwd)
SEEDOMC=$ROOT/seed/out/omc
BRIDGE=${BRIDGE:-/tmp/zl/qbridge}
[ "$(/usr/bin/sha256sum < "$OMC" | cut -d' ' -f1)" = "$PIN" ] || { echo "INVALID omc sha256 mismatch"; exit 3; }
[ ! -e "$S" ] || { echo "scratch $S exists"; exit 91; }
mkdir -p "$S"
names=${*:-CONTROL $(cd "$MD" && ls *.from | sed 's/\.from$//')}
for n in $names; do
  d=$S/$n; mkdir -p "$d"
  if [ "$n" = CONTROL ]; then cp "$UNIT" "$d/sha256-om2.om"
  else
    /usr/bin/python3 - "$UNIT" "$MD/$n.from" "$MD/$n.to" "$d/sha256-om2.om" > "$d/anchor.txt" <<'EOF'
import sys
src = open(sys.argv[1], 'rb').read(); a = open(sys.argv[2], 'rb').read(); b = open(sys.argv[3], 'rb').read()
c = src.count(a)
print(f"anchor-count={c} line={src[:src.find(a)].count(b'\n') + 1 if c else 0}")
if c != 1:
    sys.exit(1)
open(sys.argv[4], 'wb').write(src.replace(a, b))
EOF
    [ $? = 0 ] || { echo "MUTANT $n INVALID anchor $(cat "$d/anchor.txt")"; continue; }
  fi
  src=$(/usr/bin/sha256sum < "$d/sha256-om2.om" | cut -d' ' -f1)
  if ! "$OMC" < "$d/sha256-om2.om" > "$d/code.bin" 2> "$d/omc.err"; then
    echo "MUTANT $n NOT_COMPILED $(cat "$d/omc.err")"; continue
  fi
  art=$(/usr/bin/sha256sum < "$d/code.bin" | cut -d' ' -f1)
  OMEGA_NATIVE_OMC=$SEEDOMC OMEGA_NATIVE_OMC2=$OMC sh "$BRIDGE/tools/native" "$d/bind" "$d/sha256-om2.om" > "$d/native.log" 2>&1 \
    || { echo "MUTANT $n INVALID tools/native $(cat "$d/native.log")"; continue; }
  gcc -std=gnu11 -O2 -Wall -Wextra -Werror -I"$d/bind" "$DRV" -L"$d/bind" -lomega_native -Wl,-rpath,"$d/bind" -o "$d/drv" \
    || { echo "MUTANT $n INVALID driver build"; continue; }
  verdict=""
  for mode in "vec $VEC $ROOT" rcpt part "rand 748 200" bigzero big; do
    m=${mode%% *}
    # shellcheck disable=SC2086
    "$d/drv" $mode > "$d/$m.out" 2>&1; rc=$?
    if [ $rc -gt 128 ] || [ $rc = 3 ] || [ $rc = 90 ]; then verdict="INVALID mode=$m exit=$rc (signal or oracle failure)"; break; fi
    f=$(grep -m1 '^FAIL' "$d/$m.out")
    if [ -n "$f" ]; then
      t=$(grep -c '^TRAP' "$d/$m.out")
      verdict="KILLED mode=$m traps-in-mode=$t $f"; break
    fi
    t=$(grep -m1 '^TRAP' "$d/$m.out")
    [ -z "$t" ] || echo "MUTANT $n NOTE trap without wrong digest in mode $m: $t"
    [ "$n" != CONTROL ] || grep '^TOTAL' "$d/$m.out" | sed "s/^/MUTANT $n /"
  done
  [ -n "$verdict" ] || verdict=$([ "$n" = CONTROL ] && echo "PASS all modes clean" || echo "SURVIVED")
  echo "MUTANT $n src=$src artifact=$art $(cat "$d/anchor.txt" 2> /dev/null) $verdict"
done
