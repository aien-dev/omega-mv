#!/bin/sh
# E1OpenFinish: PoCL exhaustive 9 ops then exhaustive ulp EXP, LOG. Pinned to cores 5-9.
S=$SCRATCH/logs/resume-run.status
echo "$(date -u +%FT%TZ) START resume-run4 (E1OpenFinish) cores 5-9" >> $S
cd $SCRATCH/par
for op in erf gelu sin cos exp log exp2 log2 sigmoid; do
  OUT=$SCRATCH/par/exh/$op nice -n 19 taskset -c 5-9 ./exh.sh $op >> $SCRATCH/par/exh/SUMMARY 2>&1
  echo "$(date -u +%FT%TZ) pocl $op rc=$?" >> $S
done
echo "$(date -u +%FT%TZ) POCLDONE" >> $S
cd $GB10W
for op in EXP LOG; do
  /usr/bin/time -v env OMEGA_ULP_PROCS=5 nice -n 19 taskset -c 5-9 build/test_omega_numeric_cpu --ulp-exhaustive $op > $SCRATCH/logs/ulp-$op.log 2>$SCRATCH/logs/ulp-$op.time
  echo "$(date -u +%FT%TZ) ulp $op rc=$?" >> $S
done
echo "$(date -u +%FT%TZ) ULPDONE" >> $S
