#!/bin/sh
# gate1.sh <out dir> -- rung 1 gates (seed/README.md). Shell only; reference checks never make a byte.
#  build: seed0 (rung 0, rebuilt from its own text first) turns hex1.hex into hex1.bin
#  self:  hex1.bin on hex1.hex gives hex1.bin (hex1 reads seed-0's language the same way seed-0 does)
#  labels: hex1.bin on hex1.hex1 (the same program with every branch distance wiped and replaced by
#         labels) gives hex1.bin: hex1's label arithmetic equals my 50 hand-counted distances
#  audit: every word against our decoder and objdump (reference), 0 mismatches -> hex1.audit
#  controls: seed0 refuses the labelled copy (':' is not in its language); undefined label exit 5,
#         label named twice exit 6, mark inside a word exit 9, '@' before any word exit 10;
#         a backward and a forward reference give the hand-computed words
set -u
D=$(cd "$(dirname "$0")/.." && pwd); R=$(cd "$D/.." && pwd); O=$1; mkdir -p "$O"
fail=0; ok() { echo "PASS $*"; }; bad() { echo "FAIL $*"; fail=1; }
sh "$D/rung0/gate0.sh" "$O/rung0" > "$O/gate0.log" 2>&1 && ok "rung 0 gate (seed0 rebuilt from its text)" || bad "rung 0 gate"
S0=$O/rung0/seed0.bin; CHK=$R/build/seed/a64check
"$S0" < "$D/rung1/hex1.hex" > "$O/hex1.bin"; rc=$?; chmod +x "$O/hex1.bin"
[ $rc = 0 ] && cmp -s "$O/hex1.bin" "$D/rung1/hex1.bin" && ok "build: seed0 makes hex1.bin = committed ($(wc -c < "$O/hex1.bin") bytes)" || bad "build rc=$rc"
"$O/hex1.bin" < "$D/rung1/hex1.hex" > "$O/hex1.self"; rc=$?
[ $rc = 0 ] && cmp -s "$O/hex1.self" "$O/hex1.bin" && ok "self: hex1 rebuilds itself byte for byte" || bad "self rc=$rc"
sh "$D/rung1/mklabelled.sh" "$D/rung1/hex1.hex" > "$O/hex1.hex1"
cmp -s "$O/hex1.hex1" "$D/rung1/hex1.hex1" && ok "labelled copy = committed hex1.hex1 ($(grep -c '^:' "$O/hex1.hex1") labels, $(grep -c '^[0-9a-f]\{8\}@' "$O/hex1.hex1") branches by label)" || bad "labelled copy differs"
"$O/hex1.bin" < "$O/hex1.hex1" > "$O/hex1.lab"; rc=$?
[ $rc = 0 ] && cmp -s "$O/hex1.lab" "$O/hex1.bin" && ok "labels: hex1 on the labelled copy gives hex1.bin" || bad "labels rc=$rc"
sh "$D/check/audit.sh" "$D/rung1/hex1.hex" "$O/hex1.bin" "$CHK" > "$O/hex1.audit"; rc=$?
[ $rc = 0 ] && ok "audit $(tail -1 "$O/hex1.audit")" || bad "audit $(tail -1 "$O/hex1.audit")"
"$S0" < "$O/hex1.hex1" > /dev/null; rc=$?; [ $rc = 4 ] && ok "control: seed0 refuses the labelled copy (exit 4)" || bad "seed0 on labels exit $rc"
t() { printf "$2" | "$O/hex1.bin" > /dev/null; rc=$?; [ $rc = "$1" ] && ok "control: $3 (exit $1)" || bad "$3: exit $rc"; }
t 5 '54000000@q\n' "label used but never named"
t 6 ':a\n:a\nd503201f\n' "label named twice"
t 9 'd503@a201f\n:a\n' "mark inside a word"
t 10 '@a\n:a\nd503201f\n' "'@' before any word"
# :a at word 0; word 1 = b.al back to a (distance -1); word 2 = cbz x0 forward to b (+2); word 3 filler; :b at word 4
got=$(printf ':a\nd503201f\n5400000e@a\nb4000000@b\nd503201f\n:b\nd65f03c0\n' | "$O/hex1.bin" | od -An -v -tx4 -w4 | tr -d ' ' | tr '\n' ' ')
[ "$got" = "d503201f 54ffffee b4000040 d503201f d65f03c0 " ] && ok "backward (-1) and forward (+2) references: $got" || bad "references gave: $got"
[ $fail = 0 ] && echo "GATE1 PASS" || echo "GATE1 FAIL"
exit $fail
