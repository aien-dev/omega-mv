#!/bin/sh
# Sampled SPIR-V(PoCL) vs CPU-tier parity, E1 ops. Windows: 32 strided (k*2^27+2^25) + 10 targeted, 131072 inputs each.
. $TOOLS/env.sh
cd $OMEGA_MV
H=build/seed/spirv/opencl-host; B=${B:-$SCRATCH/b2}; N=131072
starts=""
k=0; while [ $k -lt 32 ]; do starts="$starts $((k*134217728+33554432))"; k=$((k+1)); done
starts="$starts 0 2147483648 1065353216 1056964608 1073741824 3212836864 2139029504 4286513152 1034000000 3181483648"
for op in ${OPS:-exp2 log2 sigmoid tanh rsqrt erf gelu sin cos exp log}; do
  mod=$op; case $op in sin|cos) mod=trig;; esac
  tot=0; bad=0
  for s in $starts; do
    n=$N; [ $((s+n)) -gt 4294967296 ] && n=$((4294967296-s))
    line=$($H --sweep $B/$mod.spv e1_$op $s $n 2>>$OUT.err | $PAR/par $op $s $n)
    rc=$?
    echo "$line rc=$rc" >> $OUT.log
    m=$(echo "$line" | awk -F'mismatches=' '/mismatches=/{print $2}' | tail -1)
    [ -n "$m" ] || { echo "$op NO-RESULT start=$s" >> $OUT.log; m=1; }
    tot=$((tot+n)); bad=$((bad+m))
  done
  echo "SUMMARY $op inputs=$tot windows=$(echo $starts | wc -w) mismatches=$bad"
done
