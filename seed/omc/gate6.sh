#!/bin/sh
# gate6.sh -- R6 structs. The compiler under test is omc built by om4, then by itself.
# Struct programs are not readable by om4 (it has no struct word). Results are exit
# statuses worked out in Wolfram (evidence/SEED-0/r6-structs/wolfram.txt), not copied
# from this compiler. RC=0 only when every case below ran and passed.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$R"
B=build/seed/r6
T=seed/omc/tests/struct
mkdir -p "$B"
OM4=seed/rung4/om4.bin
bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

"$OM4" < seed/omc/omc.om > "$B/stage1" 2>"$B/stage1.err" || bad "om4 refused omc.om ($(cat "$B/stage1.err"))"
chmod +x "$B/stage1"
"$B/stage1" < seed/omc/omc.om > "$B/stage2" 2>"$B/stage2.err" || bad "stage1 refused omc.om ($(cat "$B/stage2.err"))"
chmod +x "$B/stage2"
"$B/stage2" < seed/omc/omc.om > "$B/stage3" 2>"$B/stage3.err" || bad "stage2 refused omc.om ($(cat "$B/stage3.err"))"
chmod +x "$B/stage3"
# Reuse seed-native verification tools; no host hash/comparison pipeline.
for tool in elfcheck byteequal sha256 streamcopy; do
  "$B/stage3" < "seed/native/$tool.om" > "$B/$tool"
  [ -s "$B/$tool" ] || bad "empty native $tool"
  chmod +x "$B/$tool"
done
for tool in elfcheck byteequal sha256 streamcopy stage1 stage2 stage3; do
  "$B/elfcheck" < "$B/$tool" || bad "invalid ELF: $tool"
done
"$B/byteequal" 3< "$B/stage1" 4< "$B/stage2" || bad "parents differ"
"$B/byteequal" 3< "$B/stage2" 4< "$B/stage3" || bad "fixpoint differs"
s3=$("$B/sha256" < "$B/stage3") || bad "native hash failed"
[ "${#s3}" = 64 ] || bad "native hash length"
case "$s3" in *[!0-9a-f]*) bad "native hash encoding" ;; esac
say "PASS fixpoint $s3"
S=$B/stage3
mkdir -p seed/out
"$B/streamcopy" < "$S" > seed/out/omc || bad "native copy failed"
"$B/byteequal" 3< "$S" 4< seed/out/omc || bad "copied compiler differs"
chmod +x seed/out/omc
printf '%s  seed/out/omc\n' "$s3" > seed/out/omc.sha256

# om4 must still refuse a struct program (it has no such word).
set +e
"$OM4" < "$T/t1.om" > "$B/om4-t1.bin" 2>"$B/om4-t1.err"
orc=$?
set -e
[ "$orc" != 0 ] || bad "om4 accepted a struct program"
say "PASS om4 refuses struct (exit $orc)"

n=0
ok=0
run_exit() { # name file expect
  n=$((n+1))
  set +e
  "$S" < "$2" > "$B/$1.bin" 2>"$B/$1.err"
  crc=$?
  set -e
  [ "$crc" = 0 ] || bad "$1: compiler exit $crc ($(cat "$B/$1.err"))"
  "$B/elfcheck" < "$B/$1.bin" || bad "$1: invalid ELF"
  chmod +x "$B/$1.bin"
  set +e
  "$B/$1.bin" >/dev/null
  rrc=$?
  set -e
  [ "$rrc" = "$3" ] || bad "$1: ran $rrc, wanted $3"
  ok=$((ok+1))
  say "PASS $1 exit $rrc"
}
refuse() { # name file expect-error
  n=$((n+1))
  set +e
  "$S" < "$2" > "$B/$1.bin" 2>"$B/$1.err"
  crc=$?
  set -e
  [ "$crc" = "$3" ] || bad "$1: compiler exit $crc, wanted $3 ($(cat "$B/$1.err"))"
  "$B/byteequal" 3< "$B/$1.err" 4< "$T/expected/$1.txt" || bad "$1: diagnostic differs"
  ok=$((ok+1))
  say "PASS $1 error $crc"
}

run_exit t1 "$T/t1.om" 42
run_exit t1x "$T/t1x.om" 20
run_exit t1y "$T/t1y.om" 22
run_exit t2 "$T/t2.om" 6
run_exit t3 "$T/t3.om" 15
run_exit t4 "$T/t4.om" 17
run_exit t5 "$T/t5.om" 10
run_exit t6 "$T/t6.om" 21
run_exit t7 "$T/t7.om" 1
run_exit t7b "$T/t7b.om" 9
run_exit t8 "$T/t8.om" 1
run_exit t9 "$T/t9.om" 1

refuse bad_field "$T/bad_field.om" 3
refuse after_fn "$T/after_fn.om" 9
refuse dup_struct "$T/dup_struct.om" 10
refuse put_view "$T/put_view.om" 20
refuse fields17 "$T/fields17.om" 14
refuse idx_hi "$T/idx_hi.om" 21
refuse still_free "$T/still_free.om" 9
refuse field_is_struct "$T/field_is_struct.om" 10
refuse new_in_if "$T/new_in_if.om" 9
refuse no_end "$T/no_end.om" 4
refuse empty "$T/empty.om" 16
refuse dup_field "$T/dup_field.om" 10
refuse missing_val "$T/missing_val.om" 4

# Field names are read. Swapping x for y must change the exit, or the gate checks nothing.
n=$((n+1))
"$B/streamcopy" < "$T/t1x-swap.om" > "$B/mut.om" || bad "mutation fixture copy failed"
set +e
"$S" < "$B/mut.om" > "$B/mut.bin" 2>"$B/mut.err"
crc=$?
set -e
[ "$crc" = 0 ] || bad "mut: compiler exit $crc ($(cat "$B/mut.err"))"
"$B/elfcheck" < "$B/mut.bin" || bad "mut: invalid ELF"
chmod +x "$B/mut.bin"
set +e
"$B/mut.bin" >/dev/null
rrc=$?
set -e
[ "$rrc" = 22 ] || bad "mut ran $rrc, wanted 22 (field swap did not change the result)"
[ "$rrc" != 20 ] || bad "mut still returned x"
ok=$((ok+1))
say "PASS mut field-swap exit $rrc"

[ "$n" = 26 ] || bad "ran $n cases, wrote 26"
[ "$ok" = 26 ] || bad "passed $ok of $n"
say "PASS gate6 $ok cases"
say "RC=0"
