#!/bin/bash
# win1.sh (L127): 2-layer graph timeline + per-shape chip tests, run inside an open spark-window (spark-window run).
# The window holds the chip lock (spark-window --gb10 exclusive default since ~04:00Z); box.log still records memory and :18091 traffic.
set -u
W=$HOME/workspace/speed3-work; B=$HOME/workspace/targets/speed3/base; N=$HOME/workspace/targets/speed3/new2
until [ -f $N/READY ]; do sleep 30; done # the new build finished and its host + sim tests passed
D=$W/runs/win1-$(date -u +%Y%m%dT%H%MZ); mkdir -p $D; cd $D
cp $B/gpu_q38_graph_test g-base; cp $N/gpu_q38_graph_test g-new; cp $B/gpu_w4_api_test w4-base; cp $N/gpu_w4_api_test w4-new
{ sha256sum g-base g-new w4-base w4-new; echo "omega base dd5e89ff, new $(git -C $HOME/workspace/omega-speed3-w4gemv rev-parse HEAD)"; echo "window ${SPARK_WINDOW:-none}"; } > ground.txt
box() { echo "$(date -u +%FT%TZ) $1 mem_avail_kB=$(awk '/MemAvailable/{print $2}' /proc/meminfo) conn18091=$(ss -Htn state established '( sport = :18091 )' 2>/dev/null | wc -l) load=$(cut -d' ' -f1 /proc/loadavg)" >> box.log; }
run() { local name=$1; shift; box "start $name"; nice -n 0 "$@" > $name.log 2>&1; echo "rc=$?" >> $name.log; box "end $name"; }
# 1. chip correctness + per-shape timing (the new binary at pf 1 carries the pf identity and mutant checks)
run chip-new-pf1 ./w4-new --chip --reps 15 --pf 1 --out chip-new-pf1.json
run chip-new-pf0 ./w4-new --chip --reps 15 --pf 0 --out chip-new-pf0.json
run chip-new-pf2 ./w4-new --chip --reps 15 --pf 2 --out chip-new-pf2.json
run chip-base ./w4-base --chip --reps 15 --out chip-base.json
# 2. graph timeline, 2 layers, every projection and the head 4-bit; 3 rounds, order rotated
C=("base|./g-base|" "pf0|./g-new|--w4-pf 0" "pf1|./g-new|--w4-pf 1" "pf2|./g-new|--w4-pf 2" "pf1c96|./g-new|--w4-pf 1 --w4-ctas 96" "pf1c96r1|./g-new|--w4-pf 1 --w4-ctas 96 --w4-rows-small 1")
for r in 1 2 3; do
  for i in 0 1 2 3 4 5; do
    c=${C[$(( (i + r - 1) % 6 ))]}; name=${c%%|*}; rest=${c#*|}; bin=${rest%%|*}; args=${rest#*|}
    run g-r$r-$name $bin --w4 --w4-head --reps 10 --tl-reps 11 $args
  done
done
# 3. table: per-step GB/s and the weight-step sum per run
for f in g-r*.log; do
  printf "%s | %s | sum mix+o+gu+down %s us | span %s us | %s\n" "$f" \
    "$(grep -E 'timeline (mix|o_proj|gate_up|down|lm_head) ' $f | awk '{printf "%s %s us %s GB/s; ", $2, $5, $(NF-1)}')" \
    "$(grep -E 'timeline (mix|o_proj|gate_up|down) ' $f | awk '{s+=$5} END{print s}')" \
    "$(grep -m1 'timeline span' $f | awk '{print $3}')" "$(grep -o '[0-9]* checks, [0-9]* failed' $f)"
done > table.txt
grep -h "GPU_W4_API_TEST\|^  w4 " chip-*.log > chip-table.txt
echo DONE > done.txt
