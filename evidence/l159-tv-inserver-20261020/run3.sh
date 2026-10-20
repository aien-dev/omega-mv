#!/bin/bash
# L159 third window: one variable = nice. (a) isolated base+fix at nice 10 (the server's nice); (b) fixed server at nice 0, with a
# schedstat camera on its decode thread; (c) base server at nice 0 if the lease allows.
set -u
I=$HOME/workspace/investigations/2026-10-20-tv-inserver; T=$HOME/workspace/targets/l159; OUT=$I/chip3-$(date -u +%H%MZ); mkdir -p $OUT
export PATH=$HOME/.local/bin:$PATH
log() { echo "$(date -u +%FT%TZ) $*" >> $OUT/run.log; }
mine() { spark-window 2>&1 | head -1 | grep -q "$WID" || { log "WINDOW LOST ($WID), stopping"; exit 9; }; }
camera() { # $1 = label; samples the busiest thread of the server on port 18197 every 10 s: cpu ms, run-queue wait ms, slices
  while :; do P=$(pgrep -f "port 18197" | head -1); [ -n "$P" ] || { sleep 2; continue; }
    for t in /proc/$P/task/*; do echo "$(basename $t) $(cat $t/schedstat 2>/dev/null) $(cat $t/comm 2>/dev/null)"; done > /tmp/l159-cam-a.txt; sleep 10
    for t in /proc/$P/task/*; do echo "$(basename $t) $(cat $t/schedstat 2>/dev/null) $(cat $t/comm 2>/dev/null)"; done > /tmp/l159-cam-b.txt
    awk -v ts="$(date -u +%T)" -v L="$1" 'NR==FNR{a[$1]=$2" "$3" "$4; next} ($1 in a){split(a[$1],x," "); c=$2-x[1]; w=$3-x[2]; s=$4-x[3]; if (c+w>2e8) printf "CAM %s %s tid %s %s cpu_ms %.0f rq_wait_ms %.0f slices %d\n",L,ts,$1,$5,c/1e6,w/1e6,s}' /tmp/l159-cam-a.txt /tmp/l159-cam-b.txt >> $OUT/camera.txt
  done; }
log "window $WID load $(cut -d' ' -f1-3 /proc/loadavg)"
for v in base fix; do B=$T/lib-base/gpu_tcgemm_test; [ $v = fix ] && B=$T/lib-srv/gpu_tcgemm_test
  for s in q38gu q38mix q38amix q38o q38down; do mine
    spark-window run nice -n 10 $B --tv --shape $s --rows 1 --reps 64 --weight-cache 1 > $OUT/test-nice10-$v-$s.txt 2>&1
    log "test nice10 $v $s load $(cut -d' ' -f1 /proc/loadavg): $(grep -h '^TVGEMM' $OUT/test-nice10-$v-$s.txt | sed 's/TVGEMM //' | cut -c1-90)"
  done; done
for v in fix base; do BIN=$T/bin-fix/aien-serve-http; [ $v = base ] && BIN=$HOME/workspace/targets/speed5/bin-946f6fed-h4096/aien-serve-http; mine
  log "server $v nice0 start load $(cut -d' ' -f1-3 /proc/loadavg)"; camera $v & CP=$!
  NICE=0 BIN=$BIN SLOTS=32 KERNEL=tree FP8HEAD=0 PORT=18197 spark-window run $I/srv/session.sh $OUT/srv-nice0-$v n1-p3=3 n1-p0=0 >> $OUT/srv-$v.out 2>&1
  kill $CP 2>/dev/null; grep -h "spell over" $OUT/srv-nice0-$v/server.log 2>/dev/null | sed "s/^/SRV nice0 $v /" | cut -c1-300 >> $OUT/run.log
  log "server $v nice0 end load $(cut -d' ' -f1-3 /proc/loadavg)"
done
log done
