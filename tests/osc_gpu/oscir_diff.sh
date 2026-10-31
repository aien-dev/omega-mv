#!/bin/sh
# Differential test of the seeded .om OSC IR decoder/validator (seed/oscgpu/oscir.om)
# against the C it ports (osc_ir_decode + osc_ir_validate, src/compiler/osc_ir.c).
# Usage: sh tests/osc_gpu/oscir_diff.sh <omc> [<work-dir>]   (repository root; run
# `make test-osc-gpu` first: it builds omegese, omega_emit and build/osc_gpu/ir)
# Both sides print the same dump (tests/osc_gpu/oscir_dump.c is the C oracle,
# seed/oscgpu/oscirdump.om the .om driver) plus their exit status; every input of the
# corpus must give byte-identical output (cmp). Corpus:
#   kernel     every build/osc_gpu/ir/*.ir
#   badkernel  the checker2-stage broken kernels of tests/osc_gpu/omegese/bad (emitted here)
#   repo       every *.ir under tests/
#   gen        tests/osc_gpu/oscir_corpus.c (decoder corpus, refusal counterexamples,
#              valid variants, invalid units, random byte edits)
#   pfx/flip   for each kernel and badkernel IR: every prefix of length 0..64 and
#              32 single-byte flips (position k*len/32, mask 01/80/ff/10 cycling)
# Prints OMDIFF PASS n=N same=N diff=0, or OMDIFF FAIL and exits 1. OSCIR_SRC=<file> replaces
# seed/oscgpu/oscir.om (negative control: a mutated copy must give diff>0).
set -u
OMC=$1
W=${2:-build/osc_gpu/oscir_diff}
want_omc=62775d2a391cfef7cde64e387655caa9809a267093112910ec76bfa568f3712b
got=$(sha256sum "$OMC" | cut -d' ' -f1)
[ "$got" = "$want_omc" ] || { echo "OMDIFF FAIL: omc sha256 $got, want $want_omc"; exit 1; }
for t in build/omegese/omegese build/omega_emit/omega_emit; do
    [ -x "$t" ] || { echo "OMDIFF FAIL: $t missing (make test-osc-gpu first)"; exit 1; }
done
ls build/osc_gpu/ir/*.ir >/dev/null 2>&1 || { echo "OMDIFF FAIL: no build/osc_gpu/ir/*.ir (make test-osc-gpu first)"; exit 1; }
rm -rf "$W"; mkdir -p "$W/in" "$W/gen"
CC=${CC:-cc}
CF="-std=gnu11 -O2 -D_GNU_SOURCE -Isrc -Isrc/compiler -Isrc/compiler/model"

cat "${OSCIR_SRC:-seed/oscgpu/oscir.om}" seed/oscgpu/oscirdump.om > "$W/oscirdump.om"
"$OMC" < "$W/oscirdump.om" > "$W/oscirdump" 2> "$W/omc.err" || { echo "OMDIFF FAIL: omc refuses oscirdump: $(cat "$W/omc.err")"; exit 1; }
chmod +x "$W/oscirdump"
$CC $CF -o "$W/oscir_dump_c" tests/osc_gpu/oscir_dump.c src/compiler/osc_ir.c src/compiler/osc_alloc.c src/omega_om_units.c \
    || { echo "OMDIFF FAIL: C oracle does not build"; exit 1; }
LIB=$(ls src/compiler/*.c | grep -v -e '/oscc_main\.c$' -e '/osc_ir\.c$')
$CC $CF -o "$W/oscir_corpus" tests/osc_gpu/oscir_corpus.c $LIB src/omega_om_units.c \
    || { echo "OMDIFF FAIL: corpus generator does not build"; exit 1; }
"$W/oscir_corpus" "$W/gen" > "$W/gen.log" || { echo "OMDIFF FAIL: corpus generator"; exit 1; }

for f in build/osc_gpu/ir/*.ir; do cp "$f" "$W/in/kernel-$(basename "$f")"; done
while read -r file stage reason; do
    [ "$stage" = checker2 ] || continue
    b=$(basename "$file" .omegese)
    build/omegese/omegese emit "tests/osc_gpu/omegese/bad/$file" "$W/bad.json" >/dev/null 2>&1 || { echo "OMDIFF FAIL: omegese refused $file"; exit 1; }
    build/omega_emit/omega_emit emit "$W/bad.json" "$W/in/badkernel-$b.ir" >/dev/null 2>&1 || { echo "OMDIFF FAIL: omega_emit refused $file"; exit 1; }
done < tests/osc_gpu/omegese/bad/EXPECT
find tests -name '*.ir' -type f | sort | while read -r f; do cp "$f" "$W/in/repo-$(echo "$f" | tr '/' '_')"; done
for f in "$W"/gen/*.ir; do mv "$f" "$W/in/gen-$(basename "$f")"; done
for f in "$W"/in/kernel-*.ir "$W"/in/badkernel-*.ir; do
    b=$(basename "$f" .ir)
    l=0
    while [ $l -le 64 ]; do head -c $l "$f" > "$W/in/pfx-$b-$l.ir"; l=$((l + 1)); done
    len=$(wc -c < "$f")
    k=0
    while [ $k -lt 32 ]; do
        p=$((k * len / 32))
        case $((k % 4)) in 0) m=1 ;; 1) m=128 ;; 2) m=255 ;; *) m=16 ;; esac
        o=$(od -An -tu1 -j $p -N1 "$f" | tr -d ' ')
        v=$((o ^ m))
        cp "$f" "$W/in/flip-$b-$k.ir"
        printf "$(printf '\\%03o' $v)" | dd of="$W/in/flip-$b-$k.ir" bs=1 seek=$p conv=notrunc 2>/dev/null
        k=$((k + 1))
    done
done

n=0; same=0; diff=0
for cls in kernel badkernel repo gen pfx flip; do
    c=$(ls "$W"/in/$cls-*.ir 2>/dev/null | wc -l)
    [ "$c" -gt 0 ] || { echo "OMDIFF FAIL: corpus class $cls is empty"; exit 1; }
    echo "corpus $cls: $c"
done
for f in "$W"/in/*.ir; do
    n=$((n + 1))
    "$W/oscir_dump_c" "$f" > "$W/c.out" 2>&1; echo "EXIT $?" >> "$W/c.out"
    "$W/oscirdump" "$f" > "$W/o.out" 2>&1; echo "EXIT $?" >> "$W/o.out"
    if cmp -s "$W/c.out" "$W/o.out"; then same=$((same + 1)); else
        diff=$((diff + 1))
        if [ $diff -le 5 ]; then echo "DIFF $f"; diff "$W/c.out" "$W/o.out" | head -6; fi
    fi
done
if [ $diff -eq 0 ] && [ $same -eq $n ] && [ $n -gt 0 ]; then echo "OMDIFF PASS n=$n same=$same diff=0"; exit 0; fi
echo "OMDIFF FAIL n=$n same=$same diff=$diff"
exit 1
