#!/bin/bash
# run_attn256_freq_chip.sh EVIDENCE_DIR [PHYSICS_DIR]
# FORTRAN idea 2, FREQUENCY (omega-mv#570): the attn256 decode V-pass rescale branch, measured and Turing-scored.
# Profile: docs/turing/F2_FREQUENCY_TIMING_PROFILE_V0.md (v1 amendment). Unlike the other chip scripts this one takes
# the GB10 lock itself, once per chip step, and releases it in between (the live model server takes it per request).
#   0. host + simulator proofs (make test-gpu-attn256-freq): default program == base bytes, replica, counters, mutant red,
#      rescale 0 == 1 bits, scorer self test (shuffle control red)
#   1. chip check: counters == host replica in every warp, count mutant red, rescale 0 == 1 bits (normal + subnormal V)
#   2. train traces (seed 111) -> fit -> model.txt; 3. held-out traces (seed 222) -> sha256 -> score (all, intervention
#      block, in-distribution block, shuffled control) + bits (output identity across both modes)
# Verdict line last: "ATTN256_FREQ_CHIP: PASS|FAIL" (correctness steps decide; T is a result either way).
set -u
EVID="${1:?evidence dir}"; PHYS="${2:-$HOME/workspace/physics-6d7cf0d4}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"; cd "$HERE" || exit 2
OUT="${OUT_DIR:-/tmp/attn256-freq-build}"; mkdir -p "$EVID" "$OUT"
L=/tmp/aien-gb10.lock; T="$OUT/gpu_attn256_freq_test"; S="$OUT/f2_timing_score"; ok=1
nice make -s -j4 OUT_DIR="$OUT" PHYSICS_DIR="$PHYS" test-gpu-attn256-freq > "$EVID/host.log" 2>&1 || ok=0
POS=1023,4095,16383,65535
flock "$L" "$T" --chip-check > "$EVID/chip-check.log" 2>&1 || ok=0
flock "$L" "$T" --chip-traces-mixed "$EVID/train1.tsv" --classes rho0,iid,rho250,rho500 --pos $POS --reps 40 --seed 111 --tag train1 > "$EVID/train1.log" 2>&1 || ok=0
"$S" fit "$EVID/train1.tsv" "$EVID/model.txt" > "$EVID/fit.txt" || ok=0
flock "$L" "$T" --chip-traces-mixed "$EVID/held1.tsv" --classes rho0,iid,rho250,rho500,rho750,rho1000 --pos $POS --reps 40 --seed 222 --tag held1 > "$EVID/held1.log" 2>&1 || ok=0
sha256sum "$EVID/held1.tsv" > "$EVID/held1.sha256"
{ "$S" score "$EVID/model.txt" "$EVID/held1.tsv"; "$S" score "$EVID/model.txt" "$EVID/held1.tsv" --classes rho750,rho1000
  "$S" score "$EVID/model.txt" "$EVID/held1.tsv" --classes rho0,iid,rho250,rho500; "$S" score "$EVID/model.txt" "$EVID/held1.tsv" --shuffle 7; } > "$EVID/score.txt" || ok=0
"$S" bits "$EVID/train1.tsv" >> "$EVID/score.txt" || ok=0
"$S" bits "$EVID/held1.tsv" >> "$EVID/score.txt" || ok=0
"$S" modes "$EVID/held1.tsv" | sort -k3n -k1 > "$EVID/modes-held1.txt"
[ $ok = 1 ] && echo "ATTN256_FREQ_CHIP: PASS" || echo "ATTN256_FREQ_CHIP: FAIL"
