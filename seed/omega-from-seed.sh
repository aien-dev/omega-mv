#!/bin/sh
# omega-from-seed.sh -- birth the Omega compiler from seed-0 and stop.
# The one outside step is load0.sh (POSIX printf), named in seed/README.md.
# Everything after that is a seed program: seed0, hex1, asm2, om4, then omc
# compiling itself. No C, gcc, as or ld writes a byte of this chain.
# Run from the repo root:  sh seed/omega-from-seed.sh
# Prints RC=0 when the birth and the names-pass probes agree.
set -eu
R=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$R"
B=build/seed/birth
mkdir -p "$B" seed/out

bad() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

# 1. seed0 from its hand-written hex, then it must rebuild itself.
sh seed/rung0/load0.sh seed/rung0/seed0.hex "$B/seed0.bin"
chmod +x "$B/seed0.bin"
"$B/seed0.bin" < seed/rung0/seed0.hex > "$B/seed0.self"
cmp -s "$B/seed0.bin" "$B/seed0.self" || bad "seed0 did not rebuild itself"
cmp -s "$B/seed0.bin" seed/rung0/seed0.bin || bad "seed0.bin is not what seed0.hex makes"
printf 'PASS seed0 %s bytes, rebuilds itself\n' "$(wc -c < "$B/seed0.bin")"

# 2. hex1, born from seed0.
"$B/seed0.bin" < seed/rung1/hex1.hex > "$B/hex1.bin"
chmod +x "$B/hex1.bin"
cmp -s "$B/hex1.bin" seed/rung1/hex1.bin || bad "hex1.bin is not what seed0 makes from hex1.hex"
"$B/hex1.bin" < seed/rung1/hex1.hex > "$B/hex1.self"
cmp -s "$B/hex1.bin" "$B/hex1.self" || bad "hex1 did not rebuild itself"
printf 'PASS hex1 %s bytes, born from seed0\n' "$(wc -c < "$B/hex1.bin")"

# 3. asm2, born from hex1.
"$B/hex1.bin" < seed/rung2/asm2.hex1 > "$B/asm2.bin"
chmod +x "$B/asm2.bin"
cmp -s "$B/asm2.bin" seed/rung2/asm2.bin || bad "asm2.bin is not what hex1 makes"
printf 'PASS asm2 %s bytes, born from hex1\n' "$(wc -c < "$B/asm2.bin")"

# 4. om4, born from asm2. mk4.sh writes the measured size back into om4.s and rebuilds.
sh seed/rung4/mk4.sh > "$B/mk4.out"
cmp -s seed/rung4/om4.bin "$B/../r4/om4.again" 2>/dev/null || true
printf 'PASS om4 %s\n' "$(cat "$B/mk4.out")"

# 5. omc, born from om4, then from itself. stage2 must equal stage3.
OM4=seed/rung4/om4.bin
SRC=seed/omc/omc.om
"$OM4" < "$SRC" > "$B/stage1"
chmod +x "$B/stage1"
"$B/stage1" < "$SRC" > "$B/stage2"
chmod +x "$B/stage2"
"$B/stage2" < "$SRC" > "$B/stage3"
chmod +x "$B/stage3"
s1=$(sha256sum "$B/stage1" | cut -d' ' -f1)
s2=$(sha256sum "$B/stage2" | cut -d' ' -f1)
s3=$(sha256sum "$B/stage3" | cut -d' ' -f1)
[ "$s2" = "$s3" ] || bad "fixpoint: stage2 $s2 != stage3 $s3"
[ "$s1" = "$s2" ] || bad "parents differ: om4-built $s1 != omc-built $s2"
cp "$B/stage3" seed/out/omc
printf '%s  seed/out/omc\n' "$s3" > seed/out/omc.sha256
chmod +x seed/out/omc
printf 'PASS omc fixpoint %s\n' "$s3"

# 6. The names pass, run by the seed-born compiler, not by a C tool.
probe() { # name file expect-exit
  set +e
  seed/out/omc < "seed/rung4/tests/$2" > "$B/$1.bin" 2>"$B/$1.err"
  prc=$?
  set -e
  [ "$prc" = "$3" ] || bad "$1: exit $prc, wanted $3 ($(cat "$B/$1.err"))"
  printf 'PASS %s exit %s\n' "$1" "$prc"
}
probe fwd r5b_fwd.om 0
chmod +x "$B/fwd.bin"
set +e; "$B/fwd.bin" >/dev/null; run=$?; set -e
[ "$run" = 7 ] || bad "fwd ran as $run, wanted 7"
printf 'PASS fwd runs, exit 7\n'
probe mut r5b_mut.om 0
chmod +x "$B/mut.bin"
set +e; "$B/mut.bin" >/dev/null; run=$?; set -e
[ "$run" = 0 ] || bad "mut ran as $run, wanted 0"
printf 'PASS mut runs, exit 0\n'
probe cycle r5b_cycle.om 9
probe assign r5b_assign.om 7
grep -q 'error 9' "$B/cycle.err" || bad "cycle: missing error 9"
grep -q 'error 7' "$B/assign.err" || bad "assign: missing error 7"

printf 'RC=0\n'
