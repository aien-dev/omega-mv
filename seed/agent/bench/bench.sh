#!/bin/sh
# Paired, interleaved startup/RSS benchmark: omagent (seeded Omega) vs nanobot-ai (Python).
# Usage: sh bench.sh OMAGENT_BIN N CPU   (CPU: one core for the client; the mock runs on CPU+1)
# Output rows: arm  workload  cpu  rep  wall_ns  maxrss_kib  rc
# wall_ns is measured around `taskset ... /usr/bin/time`, so both arms carry the same launcher cost.
set -u
B=$1; N=$2; CPU=$3
D=$(cd "$(dirname "$0")" && pwd)
# nanobot-ai 0.3.5 in a venv (pip install nanobot-ai==0.3.5); scratch HOME is wiped each run.
NB=${NANOBOT:-/tmp/OmegaBot-nanobot/bin/nanobot}
H=${BENCH_HOME:-/tmp/OmegaBot-nbhome}
rm -f "$D/port"
taskset -c $((CPU + 1)) python3 "$D/mock_pong.py" "$D/port" "$D/reqs-$CPU.log" &
MP=$!
i=0; while [ ! -s "$D/port" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
P=$(cat "$D/port")
[ "$P" != 18091 ] || { kill $MP; exit 1; }
rm -rf $H; mkdir -p $H/.nanobot/workspace $H/om
cat > $H/.nanobot/config.json <<EOF
{"agents":{"defaults":{"workspace":"$H/.nanobot/workspace","model":"mock-1","provider":"custom","timezone":"UTC"}},
 "providers":{"custom":{"apiKey":"sk-test","apiBase":"http://127.0.0.1:$P/v1"}}}
EOF
# warm-up (also lets nanobot create its workspace files once)
HOME=$H $NB agent -m hi --no-markdown -c $H/.nanobot/config.json -w $H/.nanobot/workspace >/dev/null 2>&1
"$B" --url http://127.0.0.1:$P/v1/chat/completions --model mock-1 -p hi --workdir $H/om >/dev/null 2>&1
one() { # arm workload rep
  a=$1; w=$2; r=$3
  case "$a/$w" in
    omagent/version) set -- "$B" --version ;;
    nanobot/version) set -- $NB --version ;;
    omagent/turn) set -- "$B" --url http://127.0.0.1:$P/v1/chat/completions --model mock-1 -p hi --workdir $H/om ;;
    baseline/version) set -- /bin/true ;;
    nanobot/turn) set -- $NB agent -m hi --no-markdown -c $H/.nanobot/config.json -w $H/.nanobot/workspace ;;
  esac
  t0=$(date +%s%N)
  out=$(cd $H && HOME=$H taskset -c $CPU /usr/bin/time -f '%M' "$@" 2>&1 >$H/last.out)
  rc=$?
  t1=$(date +%s%N)
  m=$(printf '%s\n' "$out" | tail -n 1)
  grep -q pong $H/last.out || [ "$w" = version ] || rc=99
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$a" "$w" "$CPU" "$r" "$((t1 - t0))" "$m" "$rc"
}
for w in version turn; do
  r=1
  while [ $r -le $N ]; do
    if [ $((r % 2)) -eq 1 ]; then one omagent $w $r; one nanobot $w $r
    else one nanobot $w $r; one omagent $w $r; fi
    [ "$w" = version ] && one baseline $w $r
    r=$((r + 1))
  done
done
kill $MP
