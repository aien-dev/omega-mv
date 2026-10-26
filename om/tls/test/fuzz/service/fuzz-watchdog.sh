#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# fuzz-watchdog.sh: host orchestration (not part of the library). Pauses the om/tls fuzz workers
# (systemd --user unit tlsfuzz.service) whenever the Spark needs quiet, resumes them when every
# trigger has cleared, and stops the unit on the disk or size caps. Reads the same markers as
# ~/.local/bin/quiet-build / quiet-measure / spark-window / quietlock; never takes a lock itself
# (held locks are read from /proc/locks), never sends anything to 127.0.0.1:18091 (ss only).
#   fuzz-watchdog.sh check     one evaluation: prints "clear", "pause REASON" or "stop REASON";
#                              exit 0 / 1 / 2
#   fuzz-watchdog.sh run       loop every FUZZ_WATCH_S (10) s; STATUS + append-only LOG
#   fuzz-watchdog.sh selftest  every trigger against a synthetic marker (positive) and the same
#                              environment without it (negative control); exit 0 only if all hold
# Every path is an environment override so selftest can use synthetic markers.
set -u
H=${HOME:-/home/drakestapleton}
UNIT=${FUZZ_UNIT:-tlsfuzz.service}
FD=${FUZZ_DIR:-$H/handoffs/TlsProof/fuzz}
STATUS=${FUZZ_STATUS:-$FD/STATUS}
LOG=${FUZZ_LOG:-$FD/pause-resume.log}
CORPUS=${FUZZ_CORPUS:-$FD/corpus}
CRASHES=${FUZZ_CRASHES:-$H/handoffs/TlsProof/crashes}
WR=${SPARK_WINDOWS_ROOT:-$H/yardsticks/windows}
QL=${SPARK_QUIET_LOCK:-/tmp/aien-quiet.lock}
QG=${SPARK_QUIET_GATE:-/tmp/aien-quiet.gate}
QW=${SPARK_QUIET_WANT:-/tmp/aien-quiet.want.d}
GB=${SPARK_GB10_LOCK:-/tmp/aien-gb10.lock}
CLEAN=${FUZZ_CLEANUP_LOCK:-$H/.local/state/spark-cleanup/run.lock}
# Optional extra markers (empty by default). The timing lanes and GPU windows are already covered
# above: quiet-measure/spark-window take $QL/$QW/$QG, and every GPU window (Q27 included) holds $GB.
EQ=${FUZZ_EQ_GLOB:-}
Q27=${FUZZ_Q27_MARKERS:-}
SWEEP=${FUZZ_SWEEP_PATTERN:-}
QLBIN=${FUZZ_QUIETLOCK:-$H/.local/bin/quietlock}
PORT=${FUZZ_GUARD_PORT:-18091}
# 20-core host whose load1 ran 16.1-30.2 during the TlsProof bench (2026-10-26); the 2 niced workers stay on cpus 10-11.
LOADMAX=${FUZZ_LOAD_MAX:-16}
DISKMIN=${FUZZ_DISK_MIN_GIB:-150}
SIZEMAX=${FUZZ_SIZE_MAX_GIB:-20}

# held FILE [MODE]: some process holds a flock on FILE (MODE WRITE = exclusive only)
held() {
  [ -e "$1" ] || return 1
  i=$(stat -L -c %i "$1" 2>/dev/null) || return 1
  awk -v i=":$i\$" -v m="${2:-}" '$2=="FLOCK" && $6 ~ i && (m=="" || $4==m) {f=1} END {exit !f}' /proc/locks
}

reason() {
  [ -s "$WR/OPEN" ] && { echo "pause spark-window $(cat "$WR/OPEN")"; return; }
  for f in "$QW"/*; do
    [ -e "$f" ] || continue
    p=$(basename "$f" | sed 's/^legacy-//'); kill -0 "$p" 2>/dev/null && { echo "pause quiet-measure wanted ($f)"; return; }
  done
  held "$QL" WRITE && { echo "pause quiet lock held exclusive (timing run)"; return; }
  held "$QG" WRITE && { echo "pause quiet gate held (window draining)"; return; }
  held "$GB" && { echo "pause $GB held"; return; }
  if [ -x "$QLBIN" ] && ! "$QLBIN" check >/dev/null 2>&1; then echo "pause quietlock flag held"; return; fi
  held "$CLEAN" && { echo "pause spark-cleanup running"; return; }
  for f in $EQ; do held "$f" && { echo "pause bench lock $f held"; return; }; done
  for f in $Q27; do [ -e "$f" ] && { echo "pause Q27 GPU window ($f)"; return; }; done
  [ -n "$SWEEP" ] && pgrep -f "$SWEEP" >/dev/null 2>&1 && { echo "pause sweep running ($(pgrep -af "$SWEEP" | head -1 | cut -c1-80))"; return; }
  [ -n "$(ss -ltnH "sport = :$PORT" 2>/dev/null)" ] || { echo "pause 127.0.0.1:$PORT not listening"; return; }
  l=$(cut -d' ' -f1 /proc/loadavg)
  awk -v l="$l" -v m="$LOADMAX" 'BEGIN{exit !(l>m)}' && { echo "pause load1 $l > $LOADMAX"; return; }
  echo clear
}

check() {
  a=$(df -B1G --output=avail / | tail -1 | tr -d ' ')
  [ "$a" -lt "$DISKMIN" ] && { echo "stop / free ${a} GiB < $DISKMIN GiB"; return 2; }
  s=$(du -sb "$CORPUS" "$CRASHES" 2>/dev/null | awk '{t+=$1} END{print t+0}')
  awk -v s="$s" -v m="$SIZEMAX" 'BEGIN{exit !(s > m*1073741824)}' && { echo "stop corpus+crashes $s bytes > $SIZEMAX GiB"; return 2; }
  r=$(reason); echo "$r"
  [ "$r" = clear ] && return 0 || return 1
}

note() { printf '%s %s\n' "$(date -u +%FT%TZ)" "$*" >> "$LOG"; }

run() {
  mkdir -p "$(dirname "$STATUS")"; st=on
  while :; do
    r=$(check); rc=$?
    if [ $rc -eq 2 ]; then
      # cap reached: stop the workers (they stay stopped until someone restarts the unit)
      [ $st = stopped ] || { systemctl --user stop "$UNIT"; note "STOP $r"; }; st=stopped
    elif [ $rc -eq 1 ]; then
      # re-sent every tick so workers started or restarted while paused are paused too
      systemctl --user kill --signal=SIGSTOP "$UNIT" 2>/dev/null
      [ $st = on ] && note "PAUSE $r"; st=off
    elif [ $st = off ]; then
      systemctl --user kill --signal=SIGCONT "$UNIT" 2>/dev/null; st=on; note "RESUME"
    fi
    printf '%s %s %s\n' "$st" "$(date -u +%FT%TZ)" "$r" > "$STATUS.tmp" && mv -f "$STATUS.tmp" "$STATUS"
    sleep "${FUZZ_WATCH_S:-10}"
  done
}

# selftest: a clear synthetic world, then one trigger at a time
selftest() {
  T=$(mktemp -d /tmp/fuzzwd.XXXXXX); fails=0
  base() {
    export SPARK_WINDOWS_ROOT=$T/win SPARK_QUIET_LOCK=$T/ql SPARK_QUIET_GATE=$T/qg SPARK_QUIET_WANT=$T/want \
      SPARK_GB10_LOCK=$T/gb FUZZ_CLEANUP_LOCK=$T/clean FUZZ_EQ_GLOB="$T/eq_*" \
      FUZZ_Q27_MARKERS=$T/q27 FUZZ_SWEEP_PATTERN=omfz-synthetic-sweep FUZZ_QUIETLOCK=$T/quietlock \
      FUZZ_GUARD_PORT=$PORT FUZZ_LOAD_MAX=100000 FUZZ_DISK_MIN_GIB=0 FUZZ_SIZE_MAX_GIB=100000 \
      FUZZ_CORPUS=$T/corpus FUZZ_CRASHES=$T/crashes
  }
  # hold MODE FILE NAME WANT: hold a flock in a process whose pid stays $! (exec), test, release
  hold() { sh -c 'exec 9>"$2"; flock "$1" 9; exec sleep 30' _ "$1" "$2" & hp=$!; sleep 1; expect "$3" "$4"; kill $hp; wait $hp 2>/dev/null; }
  mkdir -p "$T/win" "$T/want" "$T/corpus" "$T/crashes"; : > "$T/ql"; : > "$T/qg"; : > "$T/gb"; : > "$T/clean"; : > "$T/eq_x"
  printf '#!/bin/sh\n[ -e %s/qflag ] && exit 1\nexit 0\n' "$T" > "$T/quietlock"; chmod +x "$T/quietlock"
  expect() { # NAME WANT_EXIT
    got=$(env "$0" check); rc=$?
    if [ $rc -eq "$2" ]; then echo "PASS $1 -> $got"; else echo "FAIL $1 -> rc=$rc $got (want $2)"; fails=$((fails+1)); fi
  }
  base; expect baseline-clear 0
  echo w1 > "$T/win/OPEN"; expect spark-window 1; rm -f "$T/win/OPEN"
  sleep 300 & sp=$!; : > "$T/want/$sp"; expect quiet-want-flag 1; rm -f "$T/want/$sp"; kill $sp
  : > "$T/want/999999999"; expect quiet-want-dead-pid-ignored 0; rm -f "$T/want/999999999"
  hold -x "$T/ql" quiet-lock-exclusive 1
  hold -s "$T/ql" quiet-lock-shared-build-ignored 0
  hold -x "$T/qg" quiet-gate 1
  hold -x "$T/gb" gb10-lock 1
  hold -s "$T/gb" gb10-lock-shared 1
  : > "$T/qflag"; expect quietlock-flag 1; rm -f "$T/qflag"
  hold -x "$T/clean" spark-cleanup 1
  hold -x "$T/eq_x" eq-bench-lock 1
  : > "$T/q27"; expect q27-window 1; rm -f "$T/q27"
  bash -c 'exec -a omfz-synthetic-sweep sleep 30' & sleep 1; expect alpha-r15-sweep 1; kill $! 2>/dev/null; wait $! 2>/dev/null
  FUZZ_GUARD_PORT=1; export FUZZ_GUARD_PORT; expect port-not-listening 1; base
  FUZZ_LOAD_MAX=0; export FUZZ_LOAD_MAX; expect load1 1; base
  FUZZ_DISK_MIN_GIB=100000; export FUZZ_DISK_MIN_GIB; expect disk-floor 2; base
  head -c 2048 /dev/zero > "$T/corpus/x"; FUZZ_SIZE_MAX_GIB=0; export FUZZ_SIZE_MAX_GIB; expect size-cap 2; base
  expect baseline-clear-again 0
  rm -rf "$T"; echo "selftest fails=$fails"; [ $fails -eq 0 ]
}

case "${1-}" in
  check) check ;;
  run) run ;;
  selftest) selftest ;;
  *) sed -n '2,13p' "$0"; exit 2 ;;
esac
