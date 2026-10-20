#!/bin/bash
# win2.sh (L127): whole model, 4-bit only from the L125 store (WW: 4-bit projections + 4-bit head), inside the open window.
# Arms (win2c): 4-bit only from the L125 store (default when the store exists), sc built with the local head-check workaround e097fe29. S0 pf 0, S1 pf 1.
# Speed: qwen38_omega_full golden-full (47 positions), 4 rounds rotated, spark-receipt per run. Timeline: qwen38_ab per arm.
set -u
W=$HOME/workspace/speed3-work; T=$HOME/workspace/targets/speed3; FULL=$T/sc-comb/release/qwen38_omega_full; AB=$T/sc-comb/release/qwen38_ab
M=$HOME/models/qwen3.8-27b-fp8-017b9c7a; S=$HOME/workspace/ab-w4-work/seq
[ -x $FULL ] && [ -x $AB ] || { echo "sc-comb tools missing" > $W/runs/win2-missing.txt; exit 3; }
D=$W/runs/win2c-$(date -u +%Y%m%dT%H%MZ); mkdir -p $D; cd $D
{ sha256sum $FULL $AB $T/comb/libomega_gpu.a; echo "omega comb $(git -C $HOME/workspace/omega-speed3-comb rev-parse HEAD) sc $(git -C $HOME/workspace/sc-speed3-w4gemv rev-parse HEAD)"; echo "window ${SPARK_WINDOW:-none}"; } > ground.txt
declare -A V=( [S0]="AIEN_OMEGA_W4_PF=0" [S1]="AIEN_OMEGA_W4_PF=1" )
box() { echo "$(date -u +%FT%TZ) $1 mem_avail_kB=$(awk '/MemAvailable/{print $2}' /proc/meminfo) conn18091=$(ss -Htn state established '( sport = :18091 )' 2>/dev/null | wc -l)" >> box.log; }
one() { local name=$1 envs=$2; shift 2; mkdir -p $name; box "start $name"
  ( cd $name && env $envs spark-receipt l127-$name --idle 10 -- nice -n 0 "$@" > run.log 2>&1; echo "rc=$?" >> run.log ); box "end $name"; }
mem_ok() { [ $(awk '/MemAvailable/{print $2}' /proc/meminfo) -ge 25000000 ]; }
ORD=( "S0 S1" "S1 S0" "S0 S1" "S1 S0" )
for v in S0 S1; do mem_ok && one tl-$v "${V[$v]}" $AB timeline $M $S/golden.json tl.json; done
for r in 1 2 3 4; do for v in ${ORD[$((r-1))]}; do
  if mem_ok; then one speed-$v-r$r "${V[$v]}" $FULL $M 64 $S/golden-full.json report.json; else box "skip speed-$v-r$r: MemAvailable under 25 GB"; fi
done; done
echo DONE > done.txt
