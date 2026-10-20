#!/bin/bash
# L159 fourth window: the tv wait now explicitly poll-only (lib-srv2 / bin-fix2). Isolated base vs fix2 at nice 0, then the server:
# fix2 at nice 10 (L129's configuration), fix2 at nice 0, base at nice 10 (same-window control), each with the thread camera.
set -u
I=$HOME/workspace/investigations/2026-10-20-tv-inserver; T=$HOME/workspace/targets/l159; OUT=$I/chip4-$(date -u +%H%MZ); mkdir -p $OUT
export PATH=$HOME/.local/bin:$PATH
log() { echo "$(date -u +%FT%TZ) $*" >> $OUT/run.log; }
mine() { spark-window 2>&1 | head -1 | grep -q "$WID" || { log "WINDOW LOST ($WID), stopping"; exit 9; }; }
camera() { while :; do P=$(pgrep -f "port 18197" | head -1); [ -n "$P" ] || { sleep 2; continue; }
    for t in /proc/$P/task/*; do echo "$(basename $t) $(cat $t/schedstat 2>/dev/null) $(cat $t/comm 2>/dev/null)"; done > /tmp/l159-cam-a.txt; sleep 10
    for t in /proc/$P/task/*; do echo "$(basename $t) $(cat $t/schedstat 2>/dev/null) $(cat $t/comm 2>/dev/null)"; done > /tmp/l159-cam-b.txt
    awk -v ts="$(date -u +%T)" -v L="$1" 'NR==FNR{a[$1]=$2" "$3" "$4; next} ($1 in a){split(a[$1],x," "); c=$2-x[1]; w=$3-x[2]; s=$4-x[3]; if (c+w>2e8) printf "CAM %s %s tid %s %s cpu_ms %.0f rq_wait_ms %.0f slices %d\n",L,ts,$1,$5,c/1e6,w/1e6,s}' /tmp/l159-cam-a.txt /tmp/l159-cam-b.txt >> $OUT/camera.txt
  done; }
log "window $WID load $(cut -d' ' -f1-3 /proc/loadavg) bins: $(sha256sum $T/bin-fix2/aien-serve-http | cut -c1-16) base $(sha256sum $HOME/workspace/targets/speed5/bin-946f6fed-h4096/aien-serve-http | cut -c1-16)"
for v in base fix2; do B=$T/lib-base/gpu_tcgemm_test; [ $v = fix2 ] && B=$T/lib-srv2/gpu_tcgemm_test
  for s in q38gu q38mix q38amix q38o q38down; do mine
    spark-window run $B --tv --shape $s --rows 1 --reps 64 --weight-cache 1 > $OUT/test-nice0-$v-$s.txt 2>&1
    log "test nice0 $v $s load $(cut -d' ' -f1 /proc/loadavg): $(grep -h '^TVGEMM' $OUT/test-nice0-$v-$s.txt | sed 's/TVGEMM //' | cut -c1-90)"
  done; done
run_server() { # $1 label $2 nice $3 binary
  mine; log "server $1 nice$2 start load $(cut -d' ' -f1-3 /proc/loadavg)"; camera $1 & CP=$!
  NICE=$2 BIN=$3 SLOTS=32 KERNEL=tree FP8HEAD=0 PORT=18197 spark-window run $I/srv/session.sh $OUT/srv-nice$2-$1 n1-p3=3 n1-p0=0 >> $OUT/srv-$1-nice$2.out 2>&1
  kill $CP 2>/dev/null; grep -h "spell over" $OUT/srv-nice$2-$1/server.log 2>/dev/null | sed "s/^/SRV nice$2 $1 /" | cut -c1-330 >> $OUT/run.log
  grep -h "load failed\|CHIP_FAIL" $OUT/srv-nice$2-$1/server.log 2>/dev/null | head -1 | sed "s/^/SRV nice$2 $1 /" | cut -c1-200 >> $OUT/run.log
  log "server $1 nice$2 end load $(cut -d' ' -f1-3 /proc/loadavg)"; sleep 20; }
run_server fix2 10 $T/bin-fix2/aien-serve-http
run_server fix2 0 $T/bin-fix2/aien-serve-http
run_server base 10 $HOME/workspace/targets/speed5/bin-946f6fed-h4096/aien-serve-http
log done
