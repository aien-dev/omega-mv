#!/bin/sh
# runner.sh EVDIR: L151 measurement runner. Window 1 (host phase) as soon as a spark-window can be opened; window 2
# (gpu + concurrent phases) once the GB10 lock is free at a check, or after 120 checks (the 27B server takes it per
# request and other chip jobs queue, so "free" may never be seen); then the window queues on the GB10 lock in order
# (quiet first, then gb10, the order every chip job uses, so no lock-order deadlock). Waiting = one try per minute, one ledger note per wait. Log: EVDIR/runner.log.
set -u
EV=${1:?evdir}; WT=$(cd "$(dirname "$0")/../.." && pwd); LOG=$EV/runner.log
mkdir -p "$EV"; cd "$WT" || exit 2
say() { echo "$(date -u +%FT%TZ) $*" >> "$LOG"; }
open_window() { # NAME LEASE
	noted=0
	while :; do
		out=$(spark-window open "$1" --ledger L151 --lease "$2" 2>&1) && break
		[ $noted -eq 0 ] && { say "window busy: $(echo "$out" | head -1)"; ledger note L151 "measure: waiting for window ($(spark-window status 2>/dev/null | head -1 | cut -c1-80))" > /dev/null; noted=1; }
		sleep 60
	done
	say "window open: $(spark-window id 2>/dev/null)"
}
gb10_free() { [ -z "$(spark-window locks gb10 2>/dev/null | grep -i 'holds')" ]; }

if [ ! -f "$EV/host.done" ]; then
	open_window l151-host 45
	ledger note L151 "MEASURING host phase (window $(spark-window id))" > /dev/null
	spark-window run tools/machine_ceiling/session.sh host "$EV" >> "$LOG" 2>&1; say "host rc=$?"
	spark-window close >> "$LOG" 2>&1
	touch "$EV/host.done"
fi
noted=0
tries=0
while ! gb10_free && [ $tries -lt 120 ]; do
	tries=$((tries + 1))
	[ $noted -eq 0 ] && { say "gb10 busy: $(spark-window locks gb10 2>/dev/null | head -1 | cut -c1-120)"; ledger note L151 "measure: host phase done; gpu phase waits for the GB10 lock (held by $(spark-window locks gb10 2>/dev/null | grep -o 'pid [0-9]*' | head -1))" > /dev/null; noted=1; }
	sleep 60
done
say "gb10 check done after $tries tries"
open_window l151-gpu 150
ledger note L151 "MEASURING gpu + concurrent phases (window $(spark-window id))" > /dev/null
spark-window run tools/machine_ceiling/session.sh gpu "$EV" >> "$LOG" 2>&1; say "gpu rc=$?"
spark-window run tools/machine_ceiling/session.sh concurrent "$EV" >> "$LOG" 2>&1; say "concurrent rc=$?"
spark-window close >> "$LOG" 2>&1
touch "$EV/gpu.done"
ledger note L151 "measure: all phases done, raw in $EV/raw" > /dev/null
say "done"
