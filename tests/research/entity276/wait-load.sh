#!/usr/bin/env bash
# wait-load.sh LIMIT POLL_S CAP_S LOG -- wait until the 1-minute load is under LIMIT,
# polling every POLL_S seconds for at most CAP_S seconds, then return (a timeout returns
# too: the run starts anyway). Every poll is one line in LOG.
set -u
LIMIT=${1:?}; POLL=${2:?}; CAP=${3:?}; LOG=${4:?}
t0=$(date +%s)
while :; do
  l=$(cut -d' ' -f1 /proc/loadavg)
  echo "$(date -u +%FT%TZ) load1 $l" >> "$LOG"
  awk -v l="$l" -v m="$LIMIT" 'BEGIN{exit !(l < m)}' && { echo "UNDER $LIMIT at load1 $l" >> "$LOG"; exit 0; }
  [ $(( $(date +%s) - t0 )) -ge "$CAP" ] && { echo "CAP reached, starting anyway at load1 $l" >> "$LOG"; exit 0; }
  sleep "$POLL"
done
