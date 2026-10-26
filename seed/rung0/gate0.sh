#!/bin/sh
# gate0.sh <out dir> -- rung 0 gates (seed/README.md). Shell only; the C decoder and GNU objdump/readelf
# run as reference checks and never make a byte of seed/.
#  (a) load0.sh (the one outside tool) builds seed0.bin from seed0.hex; seed0.bin run on seed0.hex
#      gives seed0.bin byte for byte, and equals the committed seed/rung0/seed0.bin
#  (b)+(c) audit.sh: every word against our decoder and objdump, 0 mismatches -> seed0.audit
#  controls: a changed name must be caught by the audit; seed0 refuses a bad character (exit 4) and a
#      cut-short word (exit 1); upper-case digits read the same as lower-case; readelf reads the header
set -u
D=$(cd "$(dirname "$0")/.." && pwd); R=$(cd "$D/.." && pwd); O=$1; mkdir -p "$O"
CHK=$R/build/seed/a64check
fail=0; ok() { echo "PASS $*"; }; bad() { echo "FAIL $*"; fail=1; }
mkdir -p "$R/build/seed"
cc -std=gnu11 -Wall -Wextra -Werror -O2 -I"$R/src" -o "$CHK" "$D/check/a64check.c" "$R/src/aarch64_decoder.c" "$R/src/aarch64_encoder.c" || { echo "FAIL a64check build"; exit 1; }
sh "$D/rung0/load0.sh" "$D/rung0/seed0.hex" "$O/seed0.bin" && chmod +x "$O/seed0.bin" || bad "load0"
"$O/seed0.bin" < "$D/rung0/seed0.hex" > "$O/seed0.self"; rc=$?
[ $rc = 0 ] && cmp -s "$O/seed0.bin" "$O/seed0.self" && ok "(a) seed0 rebuilds itself byte for byte ($(wc -c < "$O/seed0.bin") bytes)" || bad "(a) self-rebuild rc=$rc"
cmp -s "$O/seed0.bin" "$D/rung0/seed0.bin" && ok "(a) equals the committed seed/rung0/seed0.bin" || bad "(a) committed seed0.bin differs"
sh "$D/check/audit.sh" "$D/rung0/seed0.hex" "$O/seed0.bin" "$CHK" > "$O/seed0.audit"; rc=$?
[ $rc = 0 ] && ok "(b)(c) $(tail -1 "$O/seed0.audit")" || bad "(b)(c) $(tail -1 "$O/seed0.audit")"
sed 's/# \[0\]  movz x9, #10 /# [0]  movz x9, #11 /' "$D/rung0/seed0.hex" > "$O/ctl-name.hex"
sh "$D/check/audit.sh" "$O/ctl-name.hex" "$O/seed0.bin" "$CHK" > "$O/ctl-name.audit" && bad "control: wrong name not caught" || ok "control: a wrong name is caught ($(tail -1 "$O/ctl-name.audit"))"
printf 'd2800149\nd28g0039\n' | "$O/seed0.bin" > /dev/null; rc=$?; [ $rc = 4 ] && ok "control: bad character refused (exit 4)" || bad "bad char exit $rc"
printf 'd2800149\nd28000\n' | "$O/seed0.bin" > /dev/null; rc=$?; [ $rc = 1 ] && ok "control: cut-short word refused (exit 1)" || bad "short word exit $rc"
a=$(printf 'D2A00837 # x\n' | "$O/seed0.bin" | od -An -tx1 | tr -d ' '); [ "$a" = 3708a0d2 ] && ok "upper-case word D2A00837 -> bytes 37 08 a0 d2" || bad "upper-case gave $a"
readelf -h -l "$O/seed0.bin" > "$O/seed0.readelf" 2>&1 && grep -q "AArch64" "$O/seed0.readelf" && grep -q "0x400078" "$O/seed0.readelf" && ok "readelf (reference) reads the hand-written header: AArch64, entry 0x400078" || bad "readelf"
[ $fail = 0 ] && echo "GATE0 PASS" || echo "GATE0 FAIL"
exit $fail
