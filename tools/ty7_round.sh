#!/bin/sh
# One TY-7 WORK window: every prompt of round R, one request at a time.
# usage: tools/ty7_round.sh OUTDIR PROMPTS R URL
# Request body per protocol section 5. The interval logged for a reply is
# send..last byte of the attempt that was served. A request is sent only when
# the hive GPU lock (/tmp/aien-gb10.lock, which the server takes itself for
# each generation) is free at that instant, so the interval holds our
# generation and not a wait behind another chip job (amendment 1). Refusals
# (server_busy, HTTP 503 "GPU busy") are retried after 0.05 s and are not
# inside any interval.
set -u
OUT=$1; PROMPTS=$2; R=$3; URL=$4
LOCK=${TY7_GPU_LOCK:-/tmp/aien-gb10.lock}
LEAD='Continue this text in plain prose. Write only the continuation.\n\n'
i=0
while IFS= read -r line; do
	i=$((i + 1))
	esc=$(printf '%s' "$line" | sed 's/\\/\\\\/g; s/"/\\"/g')
	body=$(printf '{"messages":[{"role":"user","content":"%s%s"}],"temperature":1.0,"top_p":1.0,"top_k":0,"max_tokens":256,"seed":%d,"aien_return_token_ids":true,"stream":false}' \
		"$LEAD" "$esc" $((1000 * R + i)))
	f="$OUT/r$R-p$i.json"
	tries=0; lockwaits=0
	while :; do
		if ! flock -n "$LOCK" true; then
			lockwaits=$((lockwaits + 1)); sleep 0.05; continue
		fi
		tries=$((tries + 1))
		t0=$(date +%s%N)
		code=$(curl -s -m 1200 -o "$f" -w '%{http_code}' -H 'Content-Type: application/json' -d "$body" "$URL/v1/chat/completions")
		t1=$(date +%s%N)
		[ "$code" = 200 ] && break
		sleep 0.05
	done
	echo "$R $i $t0 $t1 http=$code tries=$tries lockwaits=$lockwaits" >> "$OUT/intervals.log"
done < "$PROMPTS"
