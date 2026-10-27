#!/bin/bash
# C1Land window886.sh (copied from C1Lang golden.sh, unchanged locked part): the last C1 step (L386). The 35B decode gate swapped to the Omegese kernel
# (AIEN_OMEGA_Q36_OMEGESE_GATE=1) must give the golden tokens; a mutated Omegese gate must change them. STATUS: NOT_RUN, needs Drake's exclusive window.
#
#   green: bin-green = sc f0230a59 (live 18091 source) + omega 2f6561d4b (live lib 6de999da + the lever; that base
#          rebuilds to the live lib sha256 110295ef). Gate code sha256 fabdfc55. Cases: 5 host goldens, 32 together vs
#          the alone reference (CURRENT 5b726818), turn 2, a tool call. PASS = every cell PASS + "omegese gate on" logged.
#   red:   bin-red = the same with the gate built from gate.omegese mutated 'exp neg get g i' -> 'exp get g i'
#          (a * sigmoid(-g); checker2 accepts it, code sha256 09522207). RED-OK = every golden differs and row 0 differs.
#
# Order: runs ONLY inside Drake's exclusive window: a BOARD line "<ts> Drake: WINDOW886 GO" or "<ts> Main: WINDOW886 GO (Drake ok <ts>)"
# must exist, posted after this script started, and the operator has already stopped 18091 (spark-release-window); this script never stops it.
# Then wait for no run_arm/run_all/hold6;
# flock -w 900 /tmp/aien-gb10.lock, held <= 20 min (BOARD GPU-START/GPU-END); refuse when any compute app is on the GPU
# or 18091 answers; port 18195 on 127.0.0.1 only; loads only through q36-extra; never 18091.
# Evidence: committed to omega-mv c1/gate-lever-reembed (draft #886), evidence/omegese-gate-lever/golden-<stamp>/, plain push.
set -u
C=/home/drakestapleton/work/C1Lang
BOARD=/home/drakestapleton/handoffs/BOARD.md
PORT=18195
LOCK=/tmp/aien-gb10.lock
MT=$C/sc-live/evidence/merge-train/merge-train
MODEL_DIR=/home/drakestapleton/models/qwen3.6-35b-a3b-fp8-95a723d0
REPO=$C/fresh886
CRUMB=$C/crumbsrc/tools/crumb/crumb
board() { echo "$(date -u +%FT%TZ) C1Land: $*" >> "$BOARD"; }
EVROOT=/home/drakestapleton/work/C1Land
say() { echo "$(date -u +%FT%TZ) $*"; }

# ---------------- the locked part: two loads, two suites (run under timeout by the outer part) ----------------
tree_of() { local c; echo "$1"; for c in $(pgrep -P "$1"); do tree_of "$c"; done; }
stop_server() { # <pidfile>
  local p pids q alive lp
  p=$(cat "$1" 2>/dev/null) && {
    pids=$(tree_of "$p" | tr '\n' ' ')
    kill $pids 2>/dev/null
    for _ in $(seq 1 90); do alive=0; for q in $pids; do kill -0 "$q" 2>/dev/null && alive=1; done; [ $alive = 0 ] && break; sleep 1; done
    kill -9 $pids 2>/dev/null; sleep 1
    say "server stopped (pids $pids)"; }
  # whatever still listens on our port (never any other port)
  lp=$(ss -ltnpH "sport = :$PORT" 2>/dev/null | grep -o 'pid=[0-9]*' | cut -d= -f2 | sort -u | tr '\n' ' ')
  [ -n "$lp" ] && { say "port $PORT still held by $lp: killing those pids"; kill $lp; sleep 5; kill -9 $lp 2>/dev/null; }
  rm -f "$1"
}
locked() { # <evidence dir>
  local EV=$1 v pidf log ok=1 i
  CUR=""; trap '[ -n "$CUR" ] && stop_server "$CUR"; exit 3' TERM INT
  for v in green red; do
    pidf=$EV/server-$v.pid; log=$EV/server-$v.log
    ss -ltnH "sport = :$PORT" | grep -q . && { say "port $PORT in use; abort"; return 2; }
    echo "=== $(date -u +%FT%TZ) golden.sh start bin-$v on 127.0.0.1:$PORT env AIEN_OMEGA_Q36_OMEGESE_GATE=1 MemAvailable $(awk '/MemAvailable/{print int($2/1048576)}' /proc/meminfo) GB" >> "$log"
    ( cd $C/bin-$v && env AIEN_OMEGA_Q36_OMEGESE_GATE=1 Q36_EXTRA_LEDGER=L386 AIEN_SERVE_KEEP_FREE_MIB=2048 setsid nohup \
        /home/drakestapleton/.local/bin/q36-extra ./aien-serve-http --engine omega --model-dir "$MODEL_DIR" --port $PORT --bind 127.0.0.1 \
        --max-context 2048 --max-tokens-cap 16384 --thinking off --batch-slots 8 >> "$log" 2>&1 < /dev/null & echo $! > "$pidf" )
    CUR=$pidf; say "$v: server pid $(cat "$pidf") starting"
    for ((i=0; i<600; i++)); do
      kill -0 "$(cat "$pidf")" 2>/dev/null || { say "$v: server exited before serving: $(tail -3 "$log" | tr '\n' ' ' | cut -c1-300)"; ok=0; break; }
      # fail closed the moment the port is up: every listener on $PORT must be 127.0.0.1
      lst=$(ss -ltnH "sport = :$PORT" 2>/dev/null | awk '{print $4}')
      if [ -n "$lst" ]; then
        bad=$(echo "$lst" | grep -vx "127.0.0.1:$PORT")
        if [ -n "$bad" ]; then say "$v: LISTENER NOT 127.0.0.1 ($(echo "$lst" | tr '\n' ' ')): killing the server"; stop_server "$pidf"; CUR=""; return 4; fi
        curl -s -m 5 "http://127.0.0.1:$PORT/v1/models" | grep -q '"id"' && break
      fi
      sleep 1
    done
    [ $ok = 1 ] && [ $i -ge 600 ] && { say "$v: not serving after 600 s"; ok=0; }
    if [ $ok = 1 ]; then
      say "$v: serving; suite $C/suite-$v.json"
      MERGE_TRAIN_SUITE=$C/suite-$v.json MERGE_TRAIN_RUN=$EV/$v MERGE_TRAIN_BOARD=/dev/null MERGE_TRAIN_LEDGER=L386 \
        bash "$MT" suite $PORT "$C/bin-$v" > "$EV/suite-$v.out" 2>&1
      say "$v: suite exit $?"
    fi
    stop_server "$pidf"; CUR=""
    [ $ok = 1 ] || return 2
  done
  return 0
}
if [ "${1:-}" = locked ]; then locked "$2"; exit $?; fi

# ---------------- the outer part: wait, lock, check, run, record ----------------
say "window886.sh pid $$: waiting for a WINDOW886 GO line from Drake or Main on BOARD"
W0=$(date +%s); N0=$(wc -l < "$BOARD")
until tail -n +$((N0 + 1)) "$BOARD" | grep -qE '^[0-9TZ:.-]+ (Drake|Main): WINDOW886 GO'; do
  if [ $(( $(date +%s) - W0 )) -ge 36000 ]; then
    say "10 h without WINDOW886 GO; GPU untouched"
    board "window886.sh NOT_RUN (no WINDOW886 GO line in 10 h). IDLE"; exit 2
  fi
  sleep 60
done
say "GO seen: $(tail -n +$((N0 + 1)) "$BOARD" | grep -E '^[0-9TZ:.-]+ (Drake|Main): WINDOW886 GO' | tail -1 | cut -c1-200)"
while pgrep -f 'run_arm|run_all|hold6' > /dev/null; do sleep 30; done
say "no run_arm/run_all/hold6 left; taking $LOCK"
until tail -1 $C/build-bins.log 2>/dev/null | grep -qE 'BUILD DONE|FAIL'; do sleep 30; done
tail -1 $C/build-bins.log | grep -q 'BUILD DONE' || { board "golden.sh ABORT: server builds failed ($C/build-bins.log); nothing loaded, NOT_RUN. IDLE"; exit 2; }
for b in $C/bin-green $C/bin-red; do ( cd "$b" && sha256sum -c --quiet SHA256SUMS ) || { board "golden.sh ABORT: $b SHA256SUMS mismatch; nothing loaded. IDLE"; exit 2; }; done
exec 9> "$LOCK"
got=0; for _ in $(seq 1 12); do flock -w 900 9 && { got=1; break; }; say "lock busy after 900 s; again"; done
[ $got = 1 ] || { board "golden.sh ABORT: $LOCK not free in 3 h; NOT_RUN. IDLE"; exit 2; }
T0=$(date +%s); STAMP=$(date -u +%Y%m%dT%H%MZ)
EV=$EVROOT/golden-$STAMP; mkdir -p "$EV"
# HEAD and the evidence dir, captured under the lock
{ echo "stamp $STAMP"; echo "evidence $EV"
  echo "omega-mv c1/gate-lever-reembed $(git -C $REPO rev-parse HEAD) (local), remote $(git -C $REPO ls-remote origin refs/heads/c1/gate-lever-reembed | cut -f1)"
  echo "lib source omega $(git -C $C/lever-live rev-parse HEAD) (green), $(git -C $C/lever-red rev-parse HEAD) + red header (red); sc $(git -C $C/sc-live rev-parse HEAD)"
  echo "reference $(readlink /home/drakestapleton/.local/state/aien-q36/merge-train/refs/qwen3.6-35b-a3b-fp8-95a723d0/CURRENT): $(cat /home/drakestapleton/.local/state/aien-q36/merge-train/refs/qwen3.6-35b-a3b-fp8-95a723d0/CURRENT/alone/done)"
  for v in green red; do echo "bin-$v:"; cat $C/bin-$v/SHA256SUMS; done
  echo "nvidia-smi compute apps at lock:"; nvidia-smi --query-compute-apps=pid,process_name,used_memory --format=csv,noheader 2>&1
} > "$EV/HEADS.txt"
apps=$(nvidia-smi --query-compute-apps=pid,process_name,used_memory --format=csv,noheader 2>&1)
if [ -n "$apps" ]; then flock -u 9; board "golden.sh ABORT: GPU not empty at lock ($(echo "$apps" | tr '\n' ';' | cut -c1-160)); nothing loaded, NOT_RUN. IDLE"; exit 2; fi
curl --noproxy '*' -s -o /dev/null -m 2 http://127.0.0.1:18091/health; rc=$?
if [ $rc != 7 ]; then flock -u 9; board "golden.sh ABORT: 18091 not refused (curl rc $rc); nothing loaded, NOT_RUN. IDLE"; exit 2; fi
board "GPU-START window886.sh (L386 Omegese gate: green vs goldens + red mutated gate) 35B on 127.0.0.1:$PORT via q36-extra, <=20 min; evidence $EV"
timeout -k 30 1140 bash "$0" locked "$EV" > "$EV/locked.log" 2>&1; lrc=$?
# never leave a copy behind, whatever happened
for p in "$EV"/server-*.pid; do [ -f "$p" ] && { p2=$(cat "$p"); kill $(tree_of "$p2") 2>/dev/null; sleep 5; kill -9 $(tree_of "$p2") 2>/dev/null; rm -f "$p"; }; done
lp=$(ss -ltnpH "sport = :$PORT" 2>/dev/null | grep -o 'pid=[0-9]*' | cut -d= -f2 | sort -u | tr '\n' ' '); [ -n "$lp" ] && kill -9 $lp
flock -u 9; exec 9>&-
T1=$(date +%s)
board "GPU-END window886.sh, held $((T1 - T0)) s, locked part exit $lrc"
if [ $lrc = 4 ]; then
  board "golden.sh NOT_RUN: a listener on :$PORT was not 127.0.0.1 ($(grep -m1 'LISTENER NOT' "$EV/locked.log" | cut -c1-160)); server killed, lock released, nothing committed. IDLE"
  exit 2
fi

# ---------------- verdict ----------------
cells() { [ -f "$EV/$1/cells.jsonl" ] && jq -r .verdict "$EV/$1/cells.jsonl" | sort | uniq -c | tr '\n' ' ' | tr -s ' '; }
gn=$(jq -s 'length' "$EV/green/cells.jsonl" 2>/dev/null || echo 0); gp=$(jq -s '[.[]|select(.verdict=="PASS")]|length' "$EV/green/cells.jsonl" 2>/dev/null || echo 0)
rn=$(jq -s 'length' "$EV/red/cells.jsonl" 2>/dev/null || echo 0); rp=$(jq -s '[.[]|select(.verdict=="RED-OK")]|length' "$EV/red/cells.jsonl" 2>/dev/null || echo 0)
gproof=no; grep -q 'q36 graph: omegese gate on (code sha256 fabdfc55cf9d67bc)' "$EV/server-green.log" 2>/dev/null && gproof=yes
rproof=no; grep -q 'q36 graph: omegese gate on (code sha256 0952220784ba88d9)' "$EV/server-red.log" 2>/dev/null && rproof=yes
G=FAIL; [ "$gn" = 8 ] && [ "$gp" = 8 ] && [ $gproof = yes ] && G=PASS
R=RED-FAIL; [ "$rn" = 6 ] && [ "$rp" = 6 ] && [ $rproof = yes ] && R=RED-OK
[ "$gn" = 0 ] && G=NOT_RUN; [ "$rn" = 0 ] && R=NOT_RUN
{ echo "# L386 Omegese decode gate on the 35B: golden tokens (C1Land window886.sh from C1Lang golden.sh, $STAMP)"; echo
  echo "green (AIEN_OMEGA_Q36_OMEGESE_GATE=1, gate code fabdfc55): **$G** - $gp/$gn cells PASS ($(cells green)); log proof 'omegese gate on' fabdfc55: $gproof"
  echo "red (mutated gate a*sigmoid(-g), code 09522207): **$R** - $rp/$rn cells RED-OK ($(cells red)); log proof 'omegese gate on' 09522207: $rproof"
  echo "GB10 lock held $((T1 - T0)) s; locked part exit $lrc; port $PORT (127.0.0.1), loads via q36-extra"; echo
  echo "Builds: see HEADS.txt and BUILD-*.txt. The green lib differs from the live lib (6de999da, sha256 110295ef, rebuilt here byte-identical) only by"
  echo "the lever commits da99bf8bc edd47302f 2f6561d4b. Tables: green/verdict.md, red/verdict.md."
} > "$EV/RESULT.md"
cp $C/bin-green/BUILD.txt "$EV/BUILD-green.txt"; cp $C/bin-red/BUILD.txt "$EV/BUILD-red.txt"
cp $C/suite-green.json $C/suite-red.json $C/redgate/green.omegese $C/redgate/red.omegese "$EV/"; cp "$0" "$EV/window886.sh"; cp $C/build-bins.sh "$EV/"
find "$EV" -name '*.pid' -delete; find "$EV" -type f -size +4M -exec gzip -9 {} \;
say "verdict green $G, red $R"

# ---------------- commit to #886 (plain push, never force) ----------------
D=evidence/omegese-gate-lever/golden-$STAMP
cd $REPO || exit 2
if git pull -q --ff-only origin c1/gate-lever-reembed; then
  mkdir -p "$D" && cp -r "$EV"/. "$D"/ && git add "$D" &&
  git commit -q -m "evidence: L386 Omegese gate on the 35B vs golden tokens: green $G, red control $R (C1Land window886.sh $STAMP)

green: AIEN_OMEGA_Q36_OMEGESE_GATE=1, gate code sha256 fabdfc55, $gp/$gn merge-train cells PASS (5 host goldens,
32 together vs alone reference 5b726818, turn 2, tool call). red: gate.omegese mutated to a*sigmoid(-g), code 09522207,
$rp/$rn cells RED-OK. GB10 lock held $((T1 - T0)) s, port $PORT. Builds: sc f0230a59 + omega 2f6561d4b (live 6de999da + lever)." &&
  $CRUMB compile > "$EV/crumb.out" 2>&1 && { git diff --quiet || { git add -A '*.crumb' .crumb 2>/dev/null; git commit -q -m "crumbs: crumb compile on c1/gate-lever-reembed (pinned crumb d0f0b95d)"; }; } &&
  git push -q origin c1/gate-lever-reembed && PUSHED="pushed $(git rev-parse --short=9 HEAD) to #886 ($D)" || PUSHED="commit/push FAILED; evidence stays in $EV"
else PUSHED="branch moved (no fast-forward); not committed; evidence stays in $EV"; fi
say "$PUSHED"
board "window886.sh done: L386 Omegese gate green $G ($gp/$gn), red control $R ($rp/$rn); $PUSHED. IDLE"
