#!/bin/bash
# L258 step 3: the 35B across a real link. Three stages: Spark stage 0 (layers 0..A, motor), the Pi 5 "hub" stage 1
# (layers A+1..B, exact reference reading its own layer files from RAM, /dev/shm), Spark stage 2 (B+1..39 + lm_head,
# motor) which sends the token back to stage 0 over loopback. Two LAN hops per token (Spark -> Pi -> Spark), wired
# 1 GbE on the Pi side. The Spark's firewall admits no inbound LAN connections, so the Spark stages dial and the Pi
# listens on both of its ends. Same gate as the loopback run: tokens + routes identical to L306's goldens.
set -u
MODEL=${MODEL:-$HOME/models/qwen3.6-35b-a3b-fp8-95a723d0}
MANIFEST=${MANIFEST:-$HOME/.cache/aien/engine-35b-manifest.json}
BIN=${BIN:-$HOME/workspace/sc-two-pipelines/target/release/engine}
G=${G:-$HOME/workspace/sc-two-pipelines/evidence/q36-host-20261020}
NAMES=${NAMES:-"gen-p1.json gen-p2.json gen-p3.json gen-p4.json"}
A=${A:-19}; B=${B:-21}
STEPS=${STEPS:-32}
PI=${PI:-hub}; PI_IP=${PI_IP:-100.64.0.3}   # tailnet (WireGuard, direct over the LAN): the Pi firewall admits only tailscale0 for new ports
CELLS=${CELLS:-"pi-1 pi-4"}
NEED_KB=${NEED_KB:-62914560}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/pi-$(date -u +%Y%m%dT%H%MZ)}
mkdir -p "$OUT"
log() { echo "$(date -u +%H:%M:%SZ) $*" | tee -a "$OUT/run.log"; }
GOLDENS=$(for n in $NAMES; do printf '%s,' "$G/$n"; done); GOLDENS=${GOLDENS%,}
PG=$(for n in $NAMES; do printf '%s,' "/dev/shm/l258-goldens/$n"; done); PG=${PG%,}
log "bin $BIN sha256 $(sha256sum "$BIN" | cut -c1-16) stages 0..$A | $((A+1))..$B on $PI | $((B+1))..39 steps $STEPS"
# the Pi's share: its layer files only, in RAM (its root disk is a USB stick)
PACK=/tmp/l258-pack-$((A+1))-$B
[ -f $PACK/model.safetensors.index.json ] || $BIN pack35 "$MODEL" $((A+1)) $B $PACK 2>&1 | tee -a "$OUT/run.log"
ssh $PI "mkdir -p /dev/shm/l258-pack /dev/shm/l258-goldens; free -m | sed -n 2p" | tee -a "$OUT/run.log"
scp -q $PACK/* $PI:/dev/shm/l258-pack/ && scp -q $(for n in $NAMES; do echo $G/$n; done) $PI:/dev/shm/l258-goldens/ && scp -q $BIN $PI:/tmp/l258-engine || { log "copy to $PI FAILED"; exit 1; }
log "pi: $(ssh $PI 'du -sh /dev/shm/l258-pack | cut -f1; free -m | sed -n 2p')"
common="--manifest $MANIFEST --goldens $GOLDENS --source ring --model $MODEL"
cell() {  # name convs
  local name=$1 convs=$2 d=$OUT/$1
  mkdir -p $d
  while [ "$(awk '/MemAvailable/{print $2}' /proc/meminfo)" -lt "$NEED_KB" ] || spark-window status 2>/dev/null | grep -qE 'window .*owner=L306 .*state=(open|held)'; do sleep 60; done
  local p0=$((47300 + RANDOM % 400)) p1=$((47700 + RANDOM % 100)) p2=$((47800 + RANDOM % 100))
  log "$name: memavail_kb $(awk '/MemAvailable/{print $2}' /proc/meminfo) load $(cut -d' ' -f1-3 /proc/loadavg) ports $p0 $p1 $p2"
  ssh $PI "rm -rf /tmp/l258-s1; nohup /tmp/l258-engine stage35 --model /dev/shm/l258-pack --goldens $PG --source shards --index 1 --of 3 --first $((A+1)) --last $B --convs $convs --steps $STEPS --prev listen:0.0.0.0:$p1 --next listen:0.0.0.0:$p2 --out /tmp/l258-s1 > /tmp/l258-s1.log 2>&1 < /dev/null &"
  quiet-measure bash -c "
    $BIN stage35 $common --index 2 --of 3 --first $((B+1)) --last 39 --convs $convs --steps $STEPS --prev dial:$PI_IP:$p2 --next dial:127.0.0.1:$p0 --out $d/s2 > $d/s2.log 2>&1 &
    $BIN stage35 $common --index 0 --of 3 --first 0 --last $A --convs $convs --steps $STEPS --prev listen:127.0.0.1:$p0 --next dial:$PI_IP:$p1 --out $d/s0 > $d/s0.log 2>&1
    echo s0 rc \$?; wait; echo s2 rc \$?" > $d/cell.log 2>&1
  sleep 2
  scp -q -r $PI:/tmp/l258-s1 $d/s1 && scp -q $PI:/tmp/l258-s1.log $d/s1.log
  log "$name: $(tr '\n' ' ' < $d/cell.log) | s0: $(tail -1 $d/s0.log | cut -c1-160) | s1: $(tail -1 $d/s1.log | cut -c1-160) | s2: $(tail -1 $d/s2.log | cut -c1-160)"
  $BIN pipe-report $d/report $STEPS $d/s0 $d/s1 $d/s2 --goldens $GOLDENS > $d/report.log 2>&1; log "$name report rc $?: $(tail -1 $d/report.log)"
}
for c in $CELLS; do
  case $c in
    pi-1) cell pi-1 1 ;;
    pi-4) cell pi-4 4 ;;
  esac
done
ssh $PI 'rm -rf /dev/shm/l258-pack /dev/shm/l258-goldens; free -m | sed -n 2p' | tee -a "$OUT/run.log"
log done
