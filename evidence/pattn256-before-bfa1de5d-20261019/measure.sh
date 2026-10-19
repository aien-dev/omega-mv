#!/bin/bash
# measure.sh NAME -- CMD...   (run from the worktree; writes evidence/NAME/)
# Drake's rules: quiet lock exclusive then GPU lock, held only for the measurement; loadavg start and end; raw output kept.
set -u
NAME="$1"; shift; [ "$1" = "--" ] && shift
cd ~/workspace/omega-prefill || exit 2
EV="evidence/$NAME"
mkdir -p "$EV"
cp "$0" "$EV/measure.sh"
{
  echo "start $(date -u +%FT%TZ) omega=$(git rev-parse HEAD) dirty=$(git status --porcelain | wc -l)"
  echo "loadavg_start $(cat /proc/loadavg)"
  free -m | head -2
  echo "command: nice -n 10 flock -x /tmp/aien-quiet.lock flock /tmp/aien-gb10.lock $*"
} > "$EV/device.txt"
printf '%q ' "$@" > "$EV/command.txt"; echo >> "$EV/command.txt"
nice -n 10 flock -x /tmp/aien-quiet.lock flock /tmp/aien-gb10.lock \
  bash -c 'echo "loadavg_locked $(cat /proc/loadavg)" >> "$0/device.txt"; ~/.local/bin/spark-receipt "$(basename "$0")" -- "$@"; rc=$?; echo "loadavg_end $(cat /proc/loadavg)" >> "$0/device.txt"; exit $rc' \
  "$EV" "$@" > "$EV/raw.log" 2>&1
rc=$?
echo "exit $rc end $(date -u +%FT%TZ)" >> "$EV/device.txt"
R=$(ls -t ~/yardsticks/receipts/*-"$NAME".json 2>/dev/null | head -1)
[ -n "$R" ] && cp "$R" "$EV/spark-receipt.json"
(cd "$EV" && sha256sum * > SHA256SUMS 2>/dev/null)
tail -5 "$EV/raw.log"
echo "rc=$rc evidence=$EV"
exit $rc
