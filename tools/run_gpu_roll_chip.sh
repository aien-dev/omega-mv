#!/bin/bash
# tools/run_gpu_roll_chip.sh EVIDENCE_DIR [ARGS...]: omega-mv#580 chip run of build/gpu_roll_test under the GB10 lock.
# Default ARGS: --chip --out EVIDENCE_DIR/rows.csv. Writes EVIDENCE_DIR/chip.log with load, memory and sensor rows.
set -u
E="${1:?evidence dir}"; shift
mkdir -p "$E"
cd "$(dirname "$0")/.." || exit 2
ARGS=("$@"); [ ${#ARGS[@]} -eq 0 ] && ARGS=(--chip --out "$E/rows.csv")
echo "# gpu_roll_test ${ARGS[*]} queued $(date -Is) head=$(git rev-parse --short HEAD) dirty=$(git status --porcelain | wc -l) physics=$(git -C "$HOME/workspace/physics-6d7cf0d4" rev-parse --short HEAD)" > "$E/chip.log"
inner() {
  echo "# lock acquired $(date -Is) loadavg=$(cut -d' ' -f1-3 /proc/loadavg) MemAvailable_kB=$(awk '/MemAvailable/{print $2}' /proc/meminfo)"
  echo "# sensors before: $(tail -1 "$HOME/yardsticks/sensors/$(date +%F).csv")"
  timeout 900 ./build/gpu_roll_test "${ARGS[@]}"; local rc=$?
  echo "# sensors after: $(tail -1 "$HOME/yardsticks/sensors/$(date +%F).csv")"
  return $rc
}
exec 9>/tmp/aien-gb10.lock
flock -w 3000 9 || { echo "# lock wait timed out $(date -Is)" >> "$E/chip.log"; exit 3; }
inner >> "$E/chip.log" 2>&1; rc=$?
exec 9>&-
echo "# exit=$rc $(date -Is)" >> "$E/chip.log"
exit $rc
