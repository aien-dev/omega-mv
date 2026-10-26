#!/bin/sh
# OMC-EFF-1 coordinator (bench/omc_efficiency/PROTOCOL.md). This script only builds the arms, records
# provenance and starts the Omega conductor; the conductor (conductor.om) does the timing, the output
# checks and the counter reading. It computes no statistics.
#
#   sh bench/omc_efficiency/run.sh OMC OUTDIR [CPU]
#
# OMC     the omc binary under test (its sha256 is recorded; the conductor and the omc arm use it)
# OUTDIR  a new directory for the work tree and the raw evidence
# CPU     the core to pin to (default 19, a Cortex-X925 core on the GB10)
set -u
[ $# -ge 2 ] || { echo "usage: run.sh OMC OUTDIR [CPU]" >&2; exit 64; }
OMC=$(readlink -f "$1") || exit 64
OUT=$2
CPU=${3:-19}
HERE=$(cd "$(dirname "$0")" && pwd)
CASES="arith branch fib leafcall sieve isort matmul"
ROUNDS=21
WARM=3
SEED=20261026
BROUNDS=10
BWARM=1
BSEED=20261027
[ -e "$OUT" ] && { echo "run.sh: $OUT exists; one declared run per directory" >&2; exit 64; }
mkdir -p "$OUT/work/src" "$OUT/work/want" "$OUT/work/bin/omc" "$OUT/work/bin/cO2" "$OUT/work/bin/cO0" \
    "$OUT/work/bin/neg" "$OUT/work/obj" "$OUT/work/mut" || exit 70
W=$(cd "$OUT/work" && pwd)
R=$(cd "$OUT" && pwd)

# ---- provenance
{
    echo "date_utc $(date -u +%FT%TZ)"
    echo "source $(git -C "$HERE" rev-parse HEAD)"
    echo "source_dirty $(git -C "$HERE" status --porcelain -- . | wc -l)"
    echo "omc $OMC"
    echo "omc_sha256 $(sha256sum "$OMC" | cut -d' ' -f1)"
    echo "protocol_sha256 $(sha256sum "$HERE/PROTOCOL.md" | cut -d' ' -f1)"
    echo "gcc $(/usr/bin/gcc --version | head -1)"
    echo "perf $(/usr/bin/perf --version)"
    echo "kernel $(uname -srm)"
    echo "cpu_pin $CPU"
    echo "cpu_model $(lscpu -e=CPU,MAXMHZ | awk -v c="$CPU" '$1==c {print $2}') MHz max"
    echo "loadavg_start $(cat /proc/loadavg)"
    echo "rounds $ROUNDS warm $WARM seed $SEED build_rounds $BROUNDS build_warm $BWARM build_seed $BSEED"
} > "$R/provenance.txt"
( cd "$HERE" && sha256sum PROTOCOL.md conductor.om run.sh kernels/*.om kernels/*.c want/*.out ) > "$R/inputs.sha256"

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
    "$OMC" < "$W/src/$c.om" > "$W/bin/omc/$c" 2> "$R/omc-$c.err" || { echo "run.sh: omc refused $c" >&2; exit 70; }
    "$OMC" < "$W/mut/$c.om" > "$W/bin/neg/$c" 2> "$R/neg-$c.err" || { echo "run.sh: omc refused mutant $c" >&2; exit 70; }
    chmod +x "$W/bin/omc/$c" "$W/bin/neg/$c"
    for o in O2 O0; do
        ( cd "$W" && env -i /usr/bin/gcc -B/usr/bin/ -$o -static -o "bin/c$o/$c" "src/$c.c" ) || exit 70
        ( cd "$W" && env -i /usr/bin/gcc -B/usr/bin/ -$o -c -o "obj/$c.$o.o" "src/$c.c" ) || exit 70
    done
done
( cd "$W" && sha256sum conductor bin/*/* mut/*.om ) > "$R/arms.sha256"
{
    echo "case arm file_bytes text_bytes"
    for c in $CASES; do
        echo "$c omc $(stat -c %s "$W/bin/omc/$c") $(stat -c %s "$W/bin/omc/$c")"
        for o in O2 O0; do
            echo "$c c$o $(stat -c %s "$W/bin/c$o/$c") $(size -A "$W/obj/$c.$o.o" | awk '$1==".text" {print $2}')"
        done
    done
} > "$R/sizes.txt"

# ---- the declared runs: compile time first, then the emitted programs
cd "$W" || exit 70
OMCARG=$OMC
taskset -c "$CPU" nice -n 19 ./conductor build "$BROUNDS" "$BWARM" "$BSEED" "$OMCARG" 3 omc cO2 cO0 $CASES \
    > "$R/build.raw" 2> "$R/build.err"
echo "build_rc $?" >> "$R/provenance.txt"
taskset -c "$CPU" nice -n 19 ./conductor run "$ROUNDS" "$WARM" "$SEED" 1 4 omc cO2 cO0 neg $CASES \
    > "$R/run.raw" 2> "$R/run.err"
echo "run_rc $?" >> "$R/provenance.txt"
echo "loadavg_end $(cat /proc/loadavg)" >> "$R/provenance.txt"
cat "$R/provenance.txt"
tail -n 1 "$R/build.raw" "$R/run.raw"
