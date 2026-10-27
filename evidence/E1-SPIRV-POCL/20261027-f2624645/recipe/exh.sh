#!/bin/sh
# Exhaustive SPIR-V(PoCL) vs CPU-tier parity for one E1 op: 256 windows of 2^24 inputs.
# Also computes the tests/test_omega_transc.c full-domain digest of the SPIR-V outputs (chunkdig.py).
# usage: OUT=prefix B=moddir exh.sh OP   (run under nice -n 19 taskset -c 10-14)
. $TOOLS/env.sh
cd $OMEGA_MV
H=build/seed/spirv/opencl-host; B=${B:-$SCRATCH/b3}; N=16777216
op=$1; mod=$op; case $op in sin|cos) mod=trig;; esac
bad=0; fails=0; tot=0; w=${W0:-0}
while [ $w -lt ${W1:-256} ]; do
  s=$((w*N))
  line=$($H --sweep $B/$mod.spv e1_$op $s $N 2>>$OUT.err | python3 $PAR/chunkdig.py $OUT.chunks $s | $PAR/par $op $s $N)
  echo "$line" >> $OUT.log
  m=$(echo "$line" | awk -F'mismatches=' '/mismatches=/{print $2}' | tail -1)
  if [ -z "$m" ]; then echo "$op NO-RESULT start=$s" >> $OUT.log; fails=$((fails+1)); else bad=$((bad+m)); tot=$((tot+N)); fi
  w=$((w+1))
done
echo "EXHAUSTIVE $op windows=${W0:-0}..$((${W1:-256}-1)) checked=$tot mismatches=$bad no_result_windows=$fails digest=$(python3 $PAR/chunkdig.py --final $OUT.chunks)"
