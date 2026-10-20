#!/bin/bash
# L159 chip A/B inside window $WID (exported by wait-and-run2.sh). base = 22446aef lib (busy spin), fix = 3208be4b (sleep-poll).
set -u
I=$HOME/workspace/investigations/2026-10-20-tv-inserver; T=$HOME/workspace/targets/l159; OUT=$I/chip-$(date -u +%H%MZ); mkdir -p $OUT
export PATH=$HOME/.local/bin:$PATH
log() { echo "$(date -u +%FT%TZ) $*" >> $OUT/run.log; }
mine() { spark-window 2>&1 | head -1 | grep -q "$WID" || { log "WINDOW LOST ($WID), stopping"; exit 9; }; }
log "window $WID load $(cut -d' ' -f1-3 /proc/loadavg) mem $(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo) GiB"
sha256sum $T/lib-base/gpu_tcgemm_test $T/lib-srv/gpu_tcgemm_test $T/bin-fix/aien-serve-http $HOME/workspace/targets/speed5/bin-946f6fed-h4096/aien-serve-http > $OUT/bins.sha 2>&1
for round in 1 2 3; do for v in base fix; do B=$T/lib-base/gpu_tcgemm_test; [ $v = fix ] && B=$T/lib-srv/gpu_tcgemm_test
  for s in q38gu q38mix q38amix q38o q38down; do mine
    spark-window run $B --tv --shape $s --rows 1 --reps 64 --weight-cache 1 > $OUT/test-$v-$s-r$round.txt 2>&1
    log "test $v $s r$round load $(cut -d' ' -f1 /proc/loadavg): $(grep -h '^TVGEMM' $OUT/test-$v-$s-r$round.txt | sed 's/TVGEMM //' | cut -c1-90)"
  done; done; done
for v in fix base fix2; do BIN=$T/bin-fix/aien-serve-http; [ $v = base ] && BIN=$HOME/workspace/targets/speed5/bin-946f6fed-h4096/aien-serve-http; mine
  log "server $v start load $(cut -d' ' -f1-3 /proc/loadavg)"
  BIN=$BIN SLOTS=32 KERNEL=tree FP8HEAD=0 PORT=18197 spark-window run $I/srv/session.sh $OUT/srv-$v n1-p3=3 n1-p0=0 >> $OUT/srv-$v.out 2>&1
  grep -h "spell over" $OUT/srv-$v/server.log 2>/dev/null | sed "s/^/SRV $v /" | cut -c1-300 >> $OUT/run.log
  log "server $v end load $(cut -d' ' -f1-3 /proc/loadavg)"
done
log done
