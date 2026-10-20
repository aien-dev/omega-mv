#!/bin/sh
# omega-mv#584 r2 (ledger L167, AMENDMENTS A4). Usage: sh research/learning-search-584/run-r2.sh PHASE
#   build    build of record under quiet-build, binary sha256 and commit recorded
#   checks   harness checks on calibration seeds 0x5871..73 (unmeasured): decision identity,
#            mutants M1..M5, nolearn, multiplier; writes r2/checks/checks.txt
#   record   run of record: wait for load1 < 15 (poll 60 s, cap 90 min), then ONE hold of
#            quiet-measure + gb10 lock + spark-receipt over 15 interleaved runs
#   controls stale (B, C0) and nolearn (C0) on the held-out seeds, every R (unmeasured)
# Evidence: evidence/LEARNING-SEARCH-584/r2/<phase>/...
set -u
cd "$(dirname "$0")/../.." || exit 2
export PHYSICS_DIR=${PHYSICS_DIR:-$HOME/workspace/physics-pf-6d7cf0d4}
export AIENOS_LOCK_REPO=${AIENOS_LOCK_REPO:-$HOME/workspace/aienos-repo}
export OUT_DIR=${OUT_DIR:-$HOME/workspace/targets/omega-research-584-r2}
BIN=$OUT_DIR/tests-research/ls584
EV=evidence/LEARNING-SEARCH-584/r2
CK="sh research/learning-search-584/r2-check.sh"
CAL="5871 5872 5873"
HELD="5881 5882 5883 5884 5885"
HORIZONS="1 3 6"
LANES=A,A2,B,C0
PHASE=${1:-}
say() { printf '%s %s\n' "$(date -u +%H:%M:%SZ)" "$*"; }
loadline() { printf 'load1=%s mem_avail_kB=%s' "$(cut -d' ' -f1 /proc/loadavg)" "$(awk '/MemAvailable/{print $2}' /proc/meminfo)"; }
one() { # one DIR ARGS... : a single process, load recorded before and after
    d=$1; shift; mkdir -p "$d"
    echo "start $(date -u +%FT%TZ) $(loadline) lock=${LS584_LOCK:-none}" > "$d/load.txt"
    nice -n 5 "$BIN" run "$@" --out "$d" > "$d/stdout.txt" 2>&1; rc=$?
    echo "end $(date -u +%FT%TZ) $(loadline) rc=$rc" >> "$d/load.txt"
    return $rc
}
case $PHASE in
build)
    ~/.local/bin/quiet-build nice -n 10 make -j4 "$BIN" || exit 1
    mkdir -p "$EV"; sha256sum "$BIN" > "$EV/binary.sha256"; git rev-parse HEAD > "$EV/commit.txt"; git status --porcelain > "$EV/dirty.txt"
    ;;
checks)
    D=$EV/checks; mkdir -p "$D"; out=$D/checks.txt; : > "$out"
    chk() { echo "$1: $2 (want $3)" | tee -a "$out"; }
    for s in $CAL; do
        for R in $HORIZONS; do
            one "$D/i-$s-R$R" --arms $LANES --seed $s --reuse $R
            for a in A B C0; do one "$D/s-$a-$s-R$R" --arm $a --seed $s --reuse $R; chk "identity interleaved $a vs standalone s=$s R=$R" "$($CK rowdiff $D/i-$s-R$R/$a $D/s-$a-$s-R$R)" 0; done
            chk "identity A2 vs A s=$s R=$R" "$($CK rowdiff $D/i-$s-R$R/A2 $D/i-$s-R$R/A)" 0
            one "$D/nolearn-C0-$s-R$R" --arm C0 --control nolearn --seed $s --reuse $R
            chk "nolearn C0 vs A s=$s R=$R" "$($CK rowdiff $D/nolearn-C0-$s-R$R $D/s-A-$s-R$R)" 0
            [ "$R" = 1 ] || { chk "multiplier s=$s R=$R bad keys" "$($CK keycount $s $R $BIN)" 0; chk "M5 multiplier s=$s R=$R bad keys" "$($CK keycount $s $R $BIN '--mutant M5')" ">0"; }
        done
    done
    s=5871
    one "$D/M4-$s-R3" --arms $LANES --seed $s --reuse 3 --mutant M4
    chk "M4 lane leak: A2 vs A rows differing" "$($CK rowdiff $D/M4-$s-R3/A2 $D/M4-$s-R3/A)" ">0"
    one "$D/M1-C1-$s" --arm C1 --seed $s --mutant M1; chk "M1 bad rows" "$(grep -o 'mutant_bad_rows=[0-9]*' $D/M1-C1-$s/stdout.txt)" ">0"
    one "$D/M1-i-$s-R3" --arms $LANES --seed $s --reuse 3 --mutant M1; chk "M1 interleaved bad rows per lane" "$(grep -o 'mutant_bad_rows=[0-9]*' $D/M1-i-$s-R3/stdout.txt | tr '\n' ' ')" ">0 each"
    one "$D/M2-B-$s" --arm B --seed $s --mutant M2; chk "M2 B bad rows" "$(grep -o 'mutant_bad_rows=[0-9]*' $D/M2-B-$s/stdout.txt)" ">0"
    one "$D/M2-C1-$s" --arm C1 --seed $s --mutant M2; chk "M2 C1 bad rows" "$(grep -o 'mutant_bad_rows=[0-9]*' $D/M2-C1-$s/stdout.txt)" ">0"
    one "$D/M3-C1-$s" --arm C1 --seed $s --mutant M3 --restart-after 176; chk "M3 bad rows" "$(grep -o 'mutant_bad_rows=[0-9]*' $D/M3-C1-$s/stdout.txt)" ">0"
    # the sealed harness (base binary, research/learning-search-584@b557e9e7) on 0x5851: same decisions
    for a in A B C0 C1; do one "$D/sealed-harness-identity-$a-5851" --arm $a --seed 5851; chk "identity new vs 2026-10-19 calib evidence $a s=5851" "$($CK rowdiff $D/sealed-harness-identity-$a-5851 evidence/LEARNING-SEARCH-584/calib/$a-5851)" 0; done
    rm -f "$D"/*/cortex.journal "$D"/*/*/cortex.journal
    ;;
record)
    D=$EV/record; mkdir -p "$D"
    w0=$(date +%s); polls=0
    while :; do
        l=$(cut -d' ' -f1 /proc/loadavg)
        awk -v l="$l" 'BEGIN{exit !(l < 15)}' && break
        [ $(( $(date +%s) - w0 )) -ge 5400 ] && { say "load still $l after 90 min: running anyway"; break; }
        polls=$((polls + 1)); sleep 60
    done
    echo "wait_start=$(date -u -d @$w0 +%FT%TZ) wait_end=$(date -u +%FT%TZ) polls=$polls $(loadline)" > "$D/wait.txt"
    say "record: $(cat $D/wait.txt)"
    ~/.local/bin/quiet-measure flock /tmp/aien-gb10.lock ~/.local/bin/spark-receipt ls584-r2 -- env LS584_LOCK=quiet+gb10 sh "$0" record-inner
    ;;
record-inner)
    D=$EV/record
    echo "batch_start $(date -u +%FT%TZ) $(loadline)" > "$D/batch.txt"
    for R in $HORIZONS; do for s in $HELD; do one "$D/R$R-$s" --arms $LANES --seed $s --reuse $R; done; done
    echo "batch_end $(date -u +%FT%TZ) $(loadline)" >> "$D/batch.txt"
    ;;
controls)
    D=$EV/controls
    for R in $HORIZONS; do for s in $HELD; do
        for a in B C0; do one "$D/$a-$s-R$R-stale" --arm $a --seed $s --reuse $R --control stale; done
        one "$D/C0-$s-R$R-nolearn" --arm C0 --seed $s --reuse $R --control nolearn
        echo "nolearn C0 vs interleaved A s=$s R=$R: $($CK rowdiff $D/C0-$s-R$R-nolearn $EV/record/R$R-$s/A) (want 0)" | tee -a "$D/nolearn.txt"
    done; done
    rm -f "$D"/*/cortex.journal
    ;;
*) echo "usage: run-r2.sh build|checks|record|controls"; exit 2 ;;
esac
say done "$PHASE"
