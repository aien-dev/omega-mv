#!/bin/bash
# measure.sh NAME -- CMD...   (run from the worktree; writes evidence/NAME/)
# Timing under ~/.local/bin/quiet-measure (quiet lock exclusive, builds held off), GPU lock inside, held only for the run.
# loadavg at start, once both locks are held, and at the end; raw output kept.
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
  echo "command: nice -n 10 ~/.local/bin/quiet-measure flock /tmp/aien-gb10.lock spark-receipt $NAME -- $*"
} > "$EV/device.txt"
printf '%q ' "$@" > "$EV/command.txt"; echo >> "$EV/command.txt"
nice -n 10 ~/.local/bin/quiet-measure flock /tmp/aien-gb10.lock \
  bash -c 'echo "loadavg_locked $(date -u +%FT%TZ) $(cat /proc/loadavg)" >> "$0/device.txt"; ~/.local/bin/spark-receipt "$(basename "$0")" -- "$@"; rc=$?; echo "loadavg_end $(date -u +%FT%TZ) $(cat /proc/loadavg)" >> "$0/device.txt"; exit $rc' \
  "$EV" "$@" > "$EV/raw.log" 2>&1
rc=$?
echo "exit $rc end $(date -u +%FT%TZ)" >> "$EV/device.txt"
R=$(ls -t ~/yardsticks/receipts/*-"$NAME".json 2>/dev/null | head -1)
[ -n "$R" ] && cp "$R" "$EV/spark-receipt.json"
(cd "$EV" && sha256sum * > SHA256SUMS 2>/dev/null)
# publish at once (Drake: every measurement goes live, failures included)
git add "$EV" && git commit -q -m "evidence: $NAME (exit $rc)" -- "$EV" && git push -q 2>&1 | grep -v '^remote:'
tail -5 "$EV/raw.log"
echo "rc=$rc evidence=$EV"
exit $rc
