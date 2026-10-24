#!/bin/sh
# gate11.sh -- reserved-word compatibility. The benchmark corpus used bits, min, max, abs and sqrt as
# ordinary names, so those spellings are not keywords; the float words are bitcast/fsqrt/fabs/fmin/
# fmax. This gate pins both halves of that rule. RC=0 only when every case below passed.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r11
T=seed/omc/tests/keywords
S=seed/out/omc
mkdir -p "$B"
[ -x "$S" ] || { printf 'FAIL: %s missing; run sh seed/omega-from-seed.sh first\n' "$S" >&2; exit 1; }
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

set +e
"$S" < "$T/names.om" > "$B/names.bin" 2> "$B/names.err"
crc=$?
set -e
[ "$crc" = 0 ] || bad "names: compiler exit $crc ($(cat "$B/names.err"))"
chmod +x "$B/names.bin"
set +e
"$B/names.bin" > /dev/null
rrc=$?
set -e
[ "$rrc" = 10 ] || bad "names: ran $rrc, wanted 10"
say "PASS names exit 10 (bits/min/max/abs/sqrt usable as names)"

set +e
"$S" < "$T/neg_reserved.om" > "$B/neg.bin" 2> "$B/neg.err"
crc=$?
set -e
[ "$crc" = 16 ] || bad "neg_reserved: compiler exit $crc, wanted 16 ($(cat "$B/neg.err"))"
say "PASS neg_reserved error 16 (bitcast is a keyword)"

say "PASS gate11 2 cases"
exit 0
