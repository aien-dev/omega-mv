#!/bin/sh
# OMC-EFF coordinator (bench/omc_efficiency/PROTOCOL.md, corrected by PROTOCOL-2.md). This script only
# builds the arms, records provenance and starts the Omega conductor; the conductor (conductor.om) does
# the timing, the output and runtime-line checks and the counter reading. It computes no statistics.
#
#   sh bench/omc_efficiency/run.sh OMC OUTDIR [CPU]                 baseline: omc, cO2, cO0, neg
#   sh bench/omc_efficiency/run.sh --ab BASE CAND SEED OUTDIR [CPU] A/B: omcB, omcC, cO2, neg
#
# OMC/BASE  the omc under test / the baseline omc (also builds the conductor and the neg mutants)
# CAND      the candidate omc (A/B only)
# SEED      the run seed of the A/B, declared in the candidate's evidence before the run (nonzero)
# OUTDIR    a new directory for the work tree and the raw evidence
# CPU       the core to pin to (default 19, a Cortex-X925 core on the GB10)
set -u
AB=0
if [ "${1:-}" = "--ab" ]; then
    [ $# -ge 5 ] || { echo "usage: run.sh --ab BASE CAND SEED OUTDIR [CPU]" >&2; exit 64; }
    AB=1
    OMC=$(readlink -f "$2") || exit 64
    CAND=$(readlink -f "$3") || exit 64
    SEED=$4
    OUT=$5
    CPU=${6:-19}
    BSEED=$SEED
    case "$SEED" in ''|*[!0-9]*|0) echo "run.sh: SEED must be a nonzero decimal number" >&2; exit 64;; esac
else
    [ $# -ge 2 ] || { echo "usage: run.sh OMC OUTDIR [CPU]" >&2; exit 64; }
    OMC=$(readlink -f "$1") || exit 64
    CAND=
    OUT=$2
    CPU=${3:-19}
    SEED=20261026
    BSEED=20261027
fi
HERE=$(cd "$(dirname "$0")" && pwd)
CASES="arith branch fib leafcall sieve isort matmul"
ROUNDS=21
WARM=3
BROUNDS=10
BWARM=1
if [ $AB = 1 ]; then OARMS="omcB omcC"; CARMS="O2"; else OARMS="omc"; CARMS="O2 O0"; fi
[ -e "$OUT" ] && { echo "run.sh: $OUT exists; one declared run per directory" >&2; exit 64; }
mkdir -p "$OUT/work/src" "$OUT/work/want" "$OUT/work/bin/neg" "$OUT/work/obj" "$OUT/work/mut" "$OUT/disasm-c" || exit 70
for a in $OARMS; do mkdir -p "$OUT/work/bin/$a" || exit 70; done
for o in $CARMS; do mkdir -p "$OUT/work/bin/c$o" || exit 70; done
W=$(cd "$OUT/work" && pwd)
R=$(cd "$OUT" && pwd)
omcof() { # arm -> its compiler
    if [ "$1" = omcC ]; then echo "$CAND"; else echo "$OMC"; fi
}

# ---- provenance
{
    echo "date_utc $(date -u +%FT%TZ)"
    echo "mode $([ $AB = 1 ] && echo ab || echo baseline)"
    echo "source $(git -C "$HERE" rev-parse HEAD)"
    echo "source_dirty $(git -C "$HERE" status --porcelain -- . | wc -l)"
    echo "omc $OMC"
    echo "omc_sha256 $(sha256sum "$OMC" | cut -d' ' -f1)"
    [ $AB = 1 ] && echo "cand $CAND" && echo "cand_sha256 $(sha256sum "$CAND" | cut -d' ' -f1)"
    echo "protocol_sha256 $(sha256sum "$HERE/PROTOCOL.md" | cut -d' ' -f1)"
    echo "protocol2_sha256 $(sha256sum "$HERE/PROTOCOL-2.md" | cut -d' ' -f1)"
    echo "gcc $(/usr/bin/gcc --version | head -1)"
    echo "perf $(/usr/bin/perf --version)"
    echo "kernel $(uname -srm)"
    echo "cpu_pin $CPU"
    echo "cpu_model $(lscpu -e=CPU,MAXMHZ | awk -v c="$CPU" '$1==c {print $2}') MHz max"
    echo "loadavg_start $(cat /proc/loadavg)"
    echo "rounds $ROUNDS warm $WARM seed $SEED build_rounds $BROUNDS build_warm $BWARM build_seed $BSEED"
} > "$R/provenance.txt"
( cd "$HERE" && sha256sum PROTOCOL.md PROTOCOL-2.md conductor.om run.sh kernels/*.om kernels/*.c want/*.out ) > "$R/inputs.sha256"

# ---- negative-control mutants: one output-changing edit per kernel; each must apply exactly once
mutate() { # case from to
    n=$(grep -c -F -- "$2" "$HERE/kernels/$1.om")
    [ "$n" = 1 ] || { echo "run.sh: mutant $1: pattern found $n times" >&2; exit 70; }
    awk -v f="$2" -v t="$3" '{ i = index($0, f); if (i) $0 = substr($0, 1, i - 1) t substr($0, i + length(f)); print }' \
        "$HERE/kernels/$1.om" > "$W/mut/$1.om"
    cmp -s "$HERE/kernels/$1.om" "$W/mut/$1.om" && { echo "run.sh: mutant $1 unchanged" >&2; exit 70; }
    return 0
}
mutate arith "h = add mul h 31 i" "h = add mul h 37 i"
mutate branch "k = add k 1" "k = add k 2"
mutate fib "if lt n 2" "if lt n 3"
mutate leafcall "shr x 5" "shr x 6"
mutate sieve "m = mul p p" "m = add mul p p p"
mutate isort "put a i rem x 100000" "put a i rem x 100001"
mutate matmul "rem add mul i 3 j 11" "rem add mul i 3 j 12"

# ---- arms
"$OMC" < "$HERE/conductor.om" > "$W/conductor" 2> "$R/conductor.err" || { echo "run.sh: conductor refused" >&2; exit 70; }
chmod +x "$W/conductor"
for c in $CASES; do
    cp "$HERE/kernels/$c.om" "$HERE/kernels/$c.c" "$W/src/" && cp "$HERE/want/$c.out" "$W/want/" || exit 70
    for a in $OARMS; do
        "$(omcof $a)" < "$W/src/$c.om" > "$W/bin/$a/$c" 2> "$R/$a-$c.err" || { echo "run.sh: $a refused $c" >&2; exit 70; }
        chmod +x "$W/bin/$a/$c"
    done
    "$OMC" < "$W/mut/$c.om" > "$W/bin/neg/$c" 2> "$R/neg-$c.err" || { echo "run.sh: omc refused mutant $c" >&2; exit 70; }
    chmod +x "$W/bin/neg/$c"
    for o in $CARMS; do
        ( cd "$W" && env -i /usr/bin/gcc -B/usr/bin/ -$o -static -o "bin/c$o/$c" "src/$c.c" ) || exit 70
        ( cd "$W" && env -i /usr/bin/gcc -B/usr/bin/ -$o -c -o "obj/$c.$o.o" "src/$c.c" ) || exit 70
        # PROTOCOL-2 section 3: the timed work must sit between the two clock_gettime calls in main
        objdump -dr "$W/obj/$c.$o.o" > "$R/disasm-c/$c.$o.s" || exit 70
    done
done
( cd "$W" && sha256sum conductor bin/*/* mut/*.om ) > "$R/arms.sha256"
{
    echo "case arm file_bytes text_bytes"
    for c in $CASES; do
        for a in $OARMS; do
            echo "$c $a $(stat -c %s "$W/bin/$a/$c") $(stat -c %s "$W/bin/$a/$c")"
        done
        for o in $CARMS; do
            echo "$c c$o $(stat -c %s "$W/bin/c$o/$c") $(size -A "$W/obj/$c.$o.o" | awk '$1 ~ /^\.text/ {s += $2} END {print s}')"
        done
    done
} > "$R/sizes.txt"

# ---- the declared runs: compile time first, then the emitted programs
cd "$W" || exit 70
if [ $AB = 1 ]; then
    taskset -c "$CPU" nice -n 19 ./conductor build "$BROUNDS" "$BWARM" "$BSEED" "$OMC" 1 omcB $CASES \
        > "$R/build-B.raw" 2> "$R/build-B.err"
    echo "build_B_rc $?" >> "$R/provenance.txt"
    taskset -c "$CPU" nice -n 19 ./conductor build "$BROUNDS" "$BWARM" "$BSEED" "$CAND" 1 omcC $CASES \
        > "$R/build-C.raw" 2> "$R/build-C.err"
    echo "build_C_rc $?" >> "$R/provenance.txt"
    taskset -c "$CPU" nice -n 19 ./conductor run "$ROUNDS" "$WARM" "$SEED" 1 4 omcB omcC cO2 neg $CASES \
        > "$R/run.raw" 2> "$R/run.err"
    echo "run_rc $?" >> "$R/provenance.txt"
else
    taskset -c "$CPU" nice -n 19 ./conductor build "$BROUNDS" "$BWARM" "$BSEED" "$OMC" 3 omc cO2 cO0 $CASES \
        > "$R/build.raw" 2> "$R/build.err"
    echo "build_rc $?" >> "$R/provenance.txt"
    taskset -c "$CPU" nice -n 19 ./conductor run "$ROUNDS" "$WARM" "$SEED" 1 4 omc cO2 cO0 neg $CASES \
        > "$R/run.raw" 2> "$R/run.err"
    echo "run_rc $?" >> "$R/provenance.txt"
fi
echo "loadavg_end $(cat /proc/loadavg)" >> "$R/provenance.txt"
cat "$R/provenance.txt"
tail -n 1 "$R"/*.raw
