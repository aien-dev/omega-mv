#!/bin/bash
# L312: timing runs only on a quiet box (brief: load under 50). Sourced by the run scripts; quiet_wait blocks until the
# 1-minute load is under LOAD_MAX, checking once a minute, and prints the load it started at.
LOAD_MAX=${LOAD_MAX:-50}
quiet_wait() {
  local l
  while :; do
    l=$(cut -d' ' -f1 /proc/loadavg)
    awk -v l="$l" -v m="$LOAD_MAX" 'BEGIN{exit !(l < m)}' && { echo "load $l"; return 0; }
    sleep 60
  done
}
