#!/bin/sh
# omega-mv#584 (L048): run the frozen protocol. Usage: sh research/learning-search-584/run.sh PHASE
#   PHASE = build | dev | calib | controls | mutants | restart | sealed | all
# Every published timing runs under quiet-measure + the gb10 lock, held per run only.
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
PHASE=${1:-all}
DEV="5841 5842 5843"
CALIB="5851 5852 5853"
SEALED="5861 5862 5863 5864 5865"

say() { printf '%s %s\n' "$(date -u +%H:%M:%SZ)" "$*"; }
loadline() { printf 'load1=%s mem_avail_kB=%s\n' "$(cut -d' ' -f1 /proc/loadavg)" "$(awk '/MemAvailable/{print $2}' /proc/meminfo)"; }

measured() { # measured DIR ARGS... : one published run under the quiet + gb10 locks
    d=$1; shift
    mkdir -p "$d"
    { echo "start $(date -u +%FT%TZ) $(loadline)"; } > "$d/load.txt"
    $QM flock -w 1200 /tmp/aien-gb10.lock "$BIN" run "$@" --out "$d" > "$d/stdout.txt" 2>&1
    rc=$?
    { echo "end $(date -u +%FT%TZ) $(loadline) rc=$rc"; } >> "$d/load.txt"
    cat "$d/stdout.txt"
    return $rc
}
unmeasured() { d=$1; shift; mkdir -p "$d"; "$BIN" run "$@" --out "$d" > "$d/stdout.txt" 2>&1; cat "$d/stdout.txt"; }

case $PHASE in
build|all)
    say build; $QB nice -n 5 make -j8 "$BIN" || exit 1
    sha256sum "$BIN" > "$EV/binary.sha256"; git rev-parse HEAD > "$EV/commit.txt"; git status --porcelain > "$EV/dirty.txt"
    ;;
esac
case $PHASE in
dev|all)
    say dev seeds, unmeasured, development only
    for s in $DEV; do for a in A B C0 C1; do unmeasured "$EV/dev/$a-$s" --arm $a --seed $s; done; done
    ;;
esac
case $PHASE in
calib|all)
    say calibration
    for s in $CALIB; do for a in A B C0 C1; do measured "$EV/calib/$a-$s" --arm $a --seed $s; done; done
    ;;
esac
case $PHASE in
controls|all)
    say controls on calibration seeds
    for s in $CALIB; do
        for a in B C1; do measured "$EV/controls/$a-$s-shuffle" --arm $a --seed $s --control shuffle; done
        measured "$EV/controls/A-$s-shuffle" --arm A --seed $s --control shuffle
        measured "$EV/controls/C1-$s-nolearn" --arm C1 --seed $s --control nolearn
        for a in B C1; do measured "$EV/controls/$a-$s-stale" --arm $a --seed $s --control stale; done
        measured "$EV/controls/C1-$s-unseen" --arm C1 --seed $s --control unseen
        measured "$EV/controls/A-$s-unseen" --arm A --seed $s --control unseen
    done
    ;;
esac
case $PHASE in
mutants|all)
    say mutants, unmeasured, correctness only
    s=5851
    unmeasured "$EV/mutants/C1-$s-M1" --arm C1 --seed $s --mutant M1
    unmeasured "$EV/mutants/B-$s-M2" --arm B --seed $s --mutant M2
    unmeasured "$EV/mutants/C1-$s-M2" --arm C1 --seed $s --mutant M2
    unmeasured "$EV/mutants/C1-$s-M3" --arm C1 --seed $s --mutant M3 --restart-after 176
    ;;
esac
case $PHASE in
restart|all)
    say restart and recovery, unmeasured
    s=5851
    for a in B C1; do
        unmeasured "$EV/restart/$a-$s-cut176" --arm $a --seed $s --restart-after 176
        unmeasured "$EV/restart/$a-$s-uncut" --arm $a --seed $s
        unmeasured "$EV/restart/$a-$s-nopersist" --arm $a --seed $s --no-persist
    done
    ;;
esac
case $PHASE in
sealed)
    [ -f "$EV/SELECTION.md" ] || { echo "SELECTION.md must exist before the sealed run"; exit 3; }
    SEL=$(sed -n 's/^selected_c: *//p' "$EV/SELECTION.md" | head -1)
    [ -n "$SEL" ] || { echo "SELECTION.md names no selected_c"; exit 3; }
    say sealed, arms A B "$SEL"
    for s in $SEALED; do for a in A B $SEL; do measured "$EV/sealed/$a-$s" --arm $a --seed $s; done; done
    for s in $SEALED; do
        measured "$EV/sealed-controls/$SEL-$s-shuffle" --arm $SEL --seed $s --control shuffle
        measured "$EV/sealed-controls/$SEL-$s-stale" --arm $SEL --seed $s --control stale
        measured "$EV/sealed-controls/$SEL-$s-nolearn" --arm $SEL --seed $s --control nolearn
        measured "$EV/sealed-controls/$SEL-$s-unseen" --arm $SEL --seed $s --control unseen
    done
    ;;
esac
say done "$PHASE"
