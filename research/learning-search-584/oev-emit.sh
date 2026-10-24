#!/bin/sh
# Emit an OEV1 file (omega-mv#583 spec/offline-evidence-v1.md) describing one arm's
# per-objective lifecycle DAG from a measured calibration summary.json, for cost
# screening (FREEZE.md work item 3). Usage: oev-emit.sh ARM SUMMARY_DIR BUILD_SHA LOAD1 > out.oev
# Every t_ns is prov=measured and names the summary file it came from. Energy: none.
set -u
ARM=$1; D=$2; SHA=$3; LOAD1=$4
S=$D/summary.json
R=$D/rows.csv
med() { # med BLOCK FIELDIDX : median of a csv column over rows of a block (1-based field index)
    awk -F, -v b="$1" -v f="$2" 'NR>1 && $2==b {print $f}' "$R" | sort -n | awk '{v[NR]=$1} END{ if(!NR){print 0} else print v[int(NR/2)+1] }'
}
# column indices in rows.csv: 16 cpu_ns, 18 adapter_cpu_ns, 20 persist_cpu_ns, 21 state_bytes
first_cpu=$(med 1 16); rep_cpu=$(med 2 16); nov_cpu=$(med 3 16); comp_cpu=$(med 4 16); rec_cpu=$(med 5 16)
adapter=$(med 1 18); persist=$(med 1 20)
peak=$(sed -n 's/.*"peak_bytes": \([0-9]*\).*/\1/p' "$S"); pbytes=$(sed -n 's/.*"persist_bytes": \([0-9]*\).*/\1/p' "$S")
cat <<EOF
# OEV1: omega-mv#584 arm $ARM lifecycle DAG for one objective of each frozen block.
# Source: $S (CPU thread time, loaded host). Emitted by research/learning-search-584/oev-emit.sh.
oev version=1
machine id=spark-b87b soc=GB10 cores=20 mem_bytes=137438953472 prov=specified src=nvidia-dgx-spark-datasheet
build repo=omega-mv sha=$SHA physics=6d7cf0d4d8eb2cda7b512100ff6058e25dbb3ddf
calibration id=LS584-calib-$(date -u +%Y%m%d) taken=$(date -u +%F) valid_days=30 load1=$LOAD1 src=$S
resource id=cpu kind=compute slots=20 prov=specified src=nproc
resource id=unified_mem kind=capacity cap=137438953472 prov=specified src=nvidia-dgx-spark-datasheet
op id=adapter kind=compute engine=cpu t_ns=$adapter prov=measured src=$R:adapter_cpu_ns:block1
op id=first_solve kind=compute engine=cpu t_ns=$first_cpu prov=measured src=$R:cpu_ns:block1
op id=repeat_solve kind=compute engine=cpu t_ns=$rep_cpu prov=measured src=$R:cpu_ns:block2
op id=novel_solve kind=compute engine=cpu t_ns=$nov_cpu prov=measured src=$R:cpu_ns:block3
op id=compose_solve kind=compute engine=cpu t_ns=$comp_cpu prov=measured src=$R:cpu_ns:block4
op id=recall_solve kind=compute engine=cpu t_ns=$rec_cpu prov=measured src=$R:cpu_ns:block5
# persist: CPU time of the Cortex journal append (measured); storage bandwidth was not measured in 584, so no bytes/bw claim is made ($pbytes B appended per stream is in summary.json).
op id=persist kind=persist engine=cpu t_ns=$persist prov=measured src=$R:persist_cpu_ns:block1
dep adapter first_solve
dep first_solve persist
dep persist repeat_solve
dep repeat_solve novel_solve
dep novel_solve compose_solve
dep compose_solve recall_solve
buffer id=arm_state bytes=$peak cap=unified_mem
energy instrument=none
EOF
