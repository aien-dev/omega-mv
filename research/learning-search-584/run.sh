#!/bin/sh
# omega-mv#584 (L048): run the frozen protocol. Usage: sh research/learning-search-584/run.sh PHASE
#   PHASE = build | dev | calib | controls | mutants | restart | sealed
# Published timings (calib, controls, sealed) run as ONE batch under quiet-measure and the
# gb10 lock, held only for that batch (each batch is tens of seconds of CPU-only runs).
# If the lock is not obtained within 20 minutes the batch runs anyway and says so
# (spark-measure rule); load is recorded at start and end of every run either way.
# Evidence lands in evidence/LEARNING-SEARCH-584/<phase>/<arm>-<seed>[-control]/.
set -u
cd "$(dirname "$0")/../.." || exit 2
export PHYSICS_DIR=${PHYSICS_DIR:-$HOME/workspace/physics-pf-6d7cf0d4}
export AIENOS_LOCK_REPO=${AIENOS_LOCK_REPO:-$HOME/workspace/aienos-repo}
export OUT_DIR=${OUT_DIR:-build}
BIN=$OUT_DIR/tests-research/ls584
EV=evidence/LEARNING-SEARCH-584
QM=$HOME/.local/bin/quiet-measure
QB=$HOME/.local/bin/quiet-build
PHASE=${1:-}
DEV="5841 5842 5843"
CALIB="5851 5852 5853"
SEALED="5861 5862 5863 5864 5865"

say() { printf '%s %s\n' "$(date -u +%H:%M:%SZ)" "$*"; }
loadline() { printf 'load1=%s mem_avail_kB=%s' "$(cut -d' ' -f1 /proc/loadavg)" "$(awk '/MemAvailable/{print $2}' /proc/meminfo)"; }

one() { # one DIR ARGS... : a single run, load recorded before and after
    d=$1; shift
    mkdir -p "$d"
    echo "start $(date -u +%FT%TZ) $(loadline) lock=${LS584_LOCK:-none}" > "$d/load.txt"
    nice -n 5 "$BIN" run "$@" --out "$d" > "$d/stdout.txt" 2>&1
    rc=$?
    echo "end $(date -u +%FT%TZ) $(loadline) rc=$rc" >> "$d/load.txt"
    cat "$d/stdout.txt"
    return $rc
}

batch() { # batch PHASE : run this script's inner phase under the locks, once
    inner=$1
    if [ -n "${LS584_LOCK:-}" ]; then sh "$0" "$inner"; return $?; fi
    say "batch $inner: requesting quiet lock + gb10 lock (up to 20 min)"
    LS584_LOCK=held $QM flock -w 1200 /tmp/aien-gb10.lock sh "$0" "$inner"
    rc=$?
    if [ $rc -eq 1 ] || [ $rc -eq 124 ]; then
        say "batch $inner: lock not obtained in 20 min, running anyway with load recorded"
        LS584_LOCK=timeout-ran-anyway sh "$0" "$inner"; rc=$?
    fi
    return $rc
}

case $PHASE in
build)
    say build; $QB nice -n 5 make -j4 "$BIN" || exit 1
    mkdir -p "$EV"; sha256sum "$BIN" > "$EV/binary.sha256"; git rev-parse HEAD > "$EV/commit.txt"; git status --porcelain > "$EV/dirty.txt"
    ;;
dev)
    say dev seeds, unmeasured, development only
    for s in $DEV; do for a in A B C0 C1; do one "$EV/dev/$a-$s" --arm $a --seed $s; done; done
    ;;
calib) batch calib-inner ;;
calib-inner)
    for s in $CALIB; do for a in A B C0 C1; do one "$EV/calib/$a-$s" --arm $a --seed $s; done; done
    ;;
controls) batch controls-inner ;;
controls-inner)
    for s in $CALIB; do
        for a in A B C1; do one "$EV/controls/$a-$s-shuffle" --arm $a --seed $s --control shuffle; done
        one "$EV/controls/C1-$s-nolearn" --arm C1 --seed $s --control nolearn
        for a in B C1; do one "$EV/controls/$a-$s-stale" --arm $a --seed $s --control stale; done
        for a in A C1; do one "$EV/controls/$a-$s-unseen" --arm $a --seed $s --control unseen; done
    done
    ;;
mutants)
    say mutants, unmeasured, correctness only
    s=5851
    one "$EV/mutants/C1-$s-M1" --arm C1 --seed $s --mutant M1
    one "$EV/mutants/B-$s-M2" --arm B --seed $s --mutant M2
    one "$EV/mutants/C1-$s-M2" --arm C1 --seed $s --mutant M2
    one "$EV/mutants/C1-$s-M3" --arm C1 --seed $s --mutant M3 --restart-after 176
    ;;
restart)
    say restart and recovery, unmeasured
    s=5851
    for a in B C1; do
        one "$EV/restart/$a-$s-cut176" --arm $a --seed $s --restart-after 176
        one "$EV/restart/$a-$s-uncut" --arm $a --seed $s
        one "$EV/restart/$a-$s-nopersist" --arm $a --seed $s --no-persist
    done
    ;;
sealed)
    [ -f "$EV/SELECTION.md" ] || { echo "SELECTION.md must exist before the sealed run"; exit 3; }
    SEL=$(sed -n 's/^selected_c: *//p' "$EV/SELECTION.md" | head -1)
    [ -n "$SEL" ] || { echo "SELECTION.md names no selected_c"; exit 3; }
    LS584_SEL=$SEL batch sealed-inner ;;
sealed-inner)
    SEL=${LS584_SEL:?}
    for s in $SEALED; do for a in A B $SEL; do one "$EV/sealed/$a-$s" --arm $a --seed $s; done; done
    for s in $SEALED; do
        one "$EV/sealed-controls/$SEL-$s-shuffle" --arm $SEL --seed $s --control shuffle
        one "$EV/sealed-controls/$SEL-$s-stale" --arm $SEL --seed $s --control stale
        one "$EV/sealed-controls/$SEL-$s-nolearn" --arm $SEL --seed $s --control nolearn
        one "$EV/sealed-controls/$SEL-$s-unseen" --arm $SEL --seed $s --control unseen
        one "$EV/sealed-controls/A-$s-unseen" --arm A --seed $s --control unseen
    done
    ;;
*) echo "usage: run.sh build|dev|calib|controls|mutants|restart|sealed"; exit 2 ;;
esac
say done "$PHASE"
