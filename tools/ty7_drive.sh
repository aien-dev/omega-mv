#!/bin/sh
# TY-7 session driver (docs/turing/TURING_YIELD_TY7_PROTOCOL.md section 7).
# usage: tools/ty7_drive.sh OUTDIR PROMPTS
# Order: IDLE0 WORK1 IDLE1 WORK2 IDLE2 WORK3 IDLE3. IDLE = 60 s, no requests.
# WORK r = the prompts of round r in order, each WORK window inside its own
# spark-receipt. Health is polled every 1 s for the whole session.
# Writes OUTDIR/session.log, health.log, intervals.log, r<R>-p<I>.json.
# Never stops or restarts the server; server_busy is retried after 0.05 s
# (outside the request interval).
set -u
OUT=$1; PROMPTS=$2
URL=${TY7_URL:-http://127.0.0.1:18091}
IDLE_S=${TY7_IDLE_S:-60}
ROUNDS=${TY7_ROUNDS:-3}
mkdir -p "$OUT"
ns() { date +%s%N; }

poll_health() {
	while :; do
		t=$(ns)
		b=$(curl -s -m 1 "$URL/health" | grep -o '"busy":[a-z]*' | cut -d: -f2)
		case $b in true) b=1 ;; false) b=0 ;; *) b=E ;; esac
		echo "$t $b" >> "$OUT/health.log"
		sleep 1
	done
}

idle() {
	echo "IDLE $1 start $(ns)" >> "$OUT/session.log"
	sleep "$IDLE_S"
	echo "IDLE $1 end $(ns)" >> "$OUT/session.log"
}

poll_health &
POLL=$!
trap 'kill $POLL 2>/dev/null' EXIT INT TERM

echo "SESSION start $(ns) url=$URL prompts=$PROMPTS sha256=$(sha256sum "$PROMPTS" | cut -d' ' -f1)" >> "$OUT/session.log"
idle 0
r=1
while [ "$r" -le "$ROUNDS" ]; do
	echo "WORK $r start $(ns)" >> "$OUT/session.log"
	spark-receipt "ty7-w$r" --idle 30 -- "$(dirname "$0")/ty7_round.sh" "$OUT" "$PROMPTS" "$r" "$URL"
	echo "WORK $r end $(ns) rc=$? receipt=$HOME/yardsticks/receipts/$(date +%F)-ty7-w$r.json" >> "$OUT/session.log"
	idle "$r"
	r=$((r + 1))
done
echo "SESSION end $(ns)" >> "$OUT/session.log"
