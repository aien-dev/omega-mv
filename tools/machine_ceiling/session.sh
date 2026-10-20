#!/bin/sh
# session.sh PHASE EVDIR: one L151 machine-ceiling measurement phase, run inside an open spark-window
# (spark-window run tools/machine_ceiling/session.sh PHASE EVDIR). Every step writes its raw output to EVDIR/raw/,
# long steps go through spark-receipt (wall watts from the UPS, GPU watts) so joules per byte can be computed.
#   host        CPU caches / memory per core type, CPU SIMD rates, NVMe write + read
#   gpu         GPU units with our own kernels (each under flock /tmp/aien-gb10.lock)
#   concurrent  GPU memory stream + all 20 CPU cores memory stream + NVMe read, all for the same 30 s
# Box state (load, MemAvailable, GPU use, lock holders, top processes) is recorded before and after each phase.
set -u
PHASE=${1:?phase}; EV=${2:?evdir}
B=$(cd "$(dirname "$0")/../.." && pwd)/build
R=$EV/raw; mkdir -p "$R"
X925=5-9,15-19; A725=0-4,10-14; ALL=0-19
SCR=${MC_SCRATCH:-$HOME/yardsticks/mc-scratch}; mkdir -p "$SCR"
state() {
	{ echo "# $1 $(date -u +%FT%T.%NZ)"; cat /proc/loadavg; grep MemAvailable /proc/meminfo
	  nvidia-smi --query-gpu=utilization.gpu,power.draw,clocks.sm,clocks.mem --format=csv,noheader 2>&1
	  spark-window locks gb10 2>&1 | head -5
	  ps -eo pcpu,pid,etime,comm --sort=-pcpu | head -12; } >> "$R/box-state.txt"
}
run() { name=$1; shift; echo "## $(date -u +%FT%T.%NZ) $*" >> "$R/$name.txt"; "$@" >> "$R/$name.txt" 2>&1; echo "## rc=$?" >> "$R/$name.txt"; }
rcpt() { name=$1; shift; spark-receipt "l151-$name" --idle 20 -- "$@" > "$R/$name.txt" 2>&1; echo "## rc=$?" >> "$R/$name.txt"; }
# the same with the GB10 lock taken first, so lock waiting is not inside the energy window
rcptg() { name=$1; shift; gb10 spark-receipt "l151-$name" --idle 20 -- "$@" > "$R/$name.txt" 2>&1; echo "## rc=$?" >> "$R/$name.txt"; }
# a window opened with the chip lock (spark-window default since L154) sets AIEN_GB10_LOCK_HELD=1: never re-take it
gb10() { if [ "${AIEN_GB10_LOCK_HELD:-}" = 1 ]; then "$@"; else flock /tmp/aien-gb10.lock "$@"; fi; }

# host + chip lock held by the window (spark-window default since L154): run the chip phases first in this same window
if [ "$PHASE" = host ] && [ "${AIEN_GB10_LOCK_HELD:-}" = 1 ] && [ ! -f "$EV/gpu.done" ]; then
	sh "$0" gpu "$EV" && sh "$0" concurrent "$EV" && touch "$EV/gpu.done"
fi
state "$PHASE start"
case $PHASE in
host)
	run cpu-env sh -c 'lscpu; lscpu -C; cat /proc/sys/abi/sve_default_vector_length; for c in 0 5; do cat /sys/devices/system/cpu/cpu$c/cpufreq/scaling_cur_freq /sys/devices/system/cpu/cpu$c/cpufreq/scaling_governor; done'
	for set in 5 0 $X925 $A725 $ALL; do
		for sz in 32768 262144 2097152; do run cpu-bw "$B/mc/mc_cpu" bw "$set" "$sz" read 5; done
		n=$(echo "$set" | awk -F, '{t=0; for(i=1;i<=NF;i++){split($i,a,"-"); t+= (a[2]==""?1:a[2]-a[1]+1)} print t}')
		per=$(( 1073741824 / n )); [ $per -lt 67108864 ] && per=67108864
		for op in read write copy; do run cpu-bw "$B/mc/mc_cpu" bw "$set" "$per" $op 5; done
		for op in f32 f16 i8dot i8mm bf16mm; do run cpu-ops "$B/mc/mc_cpu" ops "$set" $op 2; done
	done
	rcpt cpu-dram-read-all20 "$B/mc/mc_cpu" bw $ALL 67108864 read 30
	rcpt nvme-write "$B/mc/mc_disk" write "$SCR/mc.bin" 32 8 1024
	for t in 4 8 16; do run nvme-read "$B/mc/mc_disk" read "$SCR/mc.bin" $t 1024; done
	rcpt nvme-read-30s "$B/mc/mc_disk" read "$SCR/mc.bin" 16 1024 30
	;;
gpu)
	# CPU memory again while this window holds the GB10 lock (no 27B server traffic in memory at the same time)
	[ "${AIEN_GB10_LOCK_HELD:-}" = 1 ] || \
	for set in 5 0 $X925 $A725 $ALL; do run cpu-bw-gb10held gb10 "$B/mc/mc_cpu" bw "$set" 67108864 read 5; done
	run gpu-ceiling gb10 "$B/gpu_ceiling_test"
	run gpu-tc-peak gb10 "$B/gpu_tc_peak_test"
	run gpu-qmma gb10 "$B/gpu_qmma_probe_test"
	run gpu-pf-mem gb10 "$B/gpu_pf_mem_test"
	run gpu-l2-reuse gb10 "$B/gpu_l2_reuse_test" --reps 10
	run gpu-launch-cost gb10 "$B/gpu_launch_cost_test"
	rcptg gpu-stream-cached-30s "$B/gpu_ceiling_test" --stream-read 30 cached
	rcptg gpu-stream-uncached-30s "$B/gpu_ceiling_test" --stream-read 30
	;;
concurrent)
	[ -f "$SCR/mc.bin" ] || "$B/mc/mc_disk" write "$SCR/mc.bin" 32 8 1024 > "$R/nvme-prep.txt" 2>&1
	# the three legs start together inside one GB10 lock hold; each prints its own start/end epoch
	rcptg all-at-once sh -c "
		export MC_START_AT=\$(( \$(date +%s) + 25 ))
		\"$B/gpu_ceiling_test\" --stream-read 30 cached > \"$R/cc-gpu.txt\" 2>&1 &
		\"$B/mc/mc_cpu\" bw $ALL 67108864 read 30 > \"$R/cc-cpu.txt\" 2>&1 &
		\"$B/mc/mc_disk\" read \"$SCR/mc.bin\" 16 1024 30 > \"$R/cc-nvme.txt\" 2>&1 &
		wait"
	;;
*) echo "unknown phase $PHASE" >&2; exit 2 ;;
esac
state "$PHASE end"
