#!/usr/bin/env bash
# tools/exp445/mask_trial.sh <base-url> <model> <out dir> <off|on> [task ...]
# Dev trial of the Omegese mask inside our engine (ledger L112; OMEGESE.md section 7 item 2): the public dev
# tasks (tests/one_layer/exp445/dev), the omegese arm, the many-shot prompt, one attempt, the sampling of
# tools/exp445/PARAMS (max_tokens, temperature, top_p, seed_base; seed = seed_of(seed_base, 1, task index),
# the same seed in both modes). Mode off sends the request run.sh sends; mode on adds the request field
# "constraint": "omegese" (aien-serve-http built with the omegese feature). One request at a time, a busy
# server (503) is retried. No repair round: one attempt each.
#
# This is a dev trial, not a protocol run: no G0, weights or held-out tasks (EXP445_PROTOCOL.md governs those).
# It reuses the protocol's pieces unchanged: reader.sh (system prompt, user turn, request, reader) and
# ola.sh experiment (the sealed harness: checker2 and the dev cases' expected outputs).
#
# Writes <out>/<mode>/<task>/{r1.req.json,r1.resp.json,r1.transport,r1.*} and <out>/<mode>/rows.tsv:
#   task seed http completion_tokens finish reader_check harness_verdict forced logits_skipped allowed rule_us rule_us_max wall_s
# Needs built: build/omegese/omegese, build/omega_emit/{omega_emit,x445}, the acceptance tools (make omegese
# omega-emit one-layer-acceptance-tools).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
die() { echo "mask_trial.sh: $*" >&2; exit 2; }
[ $# -ge 4 ] || die "usage: mask_trial.sh <base-url> <model> <out dir> <off|on> [task ...]"
BASE="$1" MODEL="$2" OUT="$3" MODE="$4"; shift 4
case "$MODE" in off) EXTRA='{}' ;; on) EXTRA='{"constraint":"omegese"}' ;; *) die "mode must be off or on" ;; esac
. "$HERE/reader.sh"
kv() { awk -F= -v k="$1" '$1 == k { print substr($0, length(k) + 2); exit }' "$HERE/PARAMS"; }
MAXT="$(kv max_tokens)" TEMP="$(kv temperature)" TOPP="$(kv top_p)" SEED="$(kv seed_base)"
B="$ROOT/build"
X="$B/omega_emit/x445" EMIT="$B/omega_emit/omega_emit" G="$B/omegese/omegese"
for t in "$X" "$EMIT" "$G"; do [ -x "$t" ] || die "not built: $t"; done
export G
DEV="$ROOT/tests/one_layer/exp445/dev/tasks/cases"
mapfile -t ALL < <(ls "$DEV" | LC_ALL=C sort)
if [ $# -gt 0 ]; then TASKS=("$@"); else TASKS=("${ALL[@]}"); fi
D="$OUT/$MODE"
mkdir -p "$D/cands/mask-$MODE"
SYS="$(system_prompt "$HERE/arms" omegese many)"
printf '%s' "$SYS" > "$OUT/system.txt"
: > "$D/rows.tsv"
for task in "${TASKS[@]}"; do
  idx="$(printf '%s\n' "${ALL[@]}" | awk -v t="$task" '$0 == t { print NR }')"
  [ -n "$idx" ] || die "no dev task $task"
  g="$D/$task"; mkdir -p "$g"
  seed="$(seed_of "$SEED" 1 "$idx")"
  msgs="$(first_msgs "$SYS" "$(user_msg "$DEV/$task/requirement" "$DEV/$task/prompt")")"
  request_json "$MODEL" "$msgs" "$MAXT" "$TEMP" "$TOPP" "$seed" "$EXTRA" > "$g/r1.req.json"
  try=0
  while :; do
    try=$((try + 1))
    t0="$(date +%s.%N)"
    code="$(curl -sS -o "$g/r1.resp.json" -w '%{http_code}' -H 'Content-Type: application/json' \
      --data-binary @"$g/r1.req.json" --max-time 1800 "$BASE/chat/completions" || echo 000)"
    t1="$(date +%s.%N)"
    echo "try=$try http=$code time_s=$(echo "$t1 - $t0" | bc)" >> "$g/r1.transport"
    [ "$code" = 503 ] || [ "$code" = 000 ] || break
    [ "$try" -lt 60 ] || die "$task: server busy after $try tries"
    sleep 20
  done
  wall="$(tail -n 1 "$g/r1.transport" | sed -n 's/.* time_s=\([^ ]*\).*/\1/p')"
  if [ "$code" = 200 ] && resp_ok "$g/r1.resp.json"; then
    content_of "$g/r1.resp.json" > "$g/r1.content"
    reader "$X" "$EMIT" omegese "$g/r1.content" "$g/r1" "$DEV/$task/requirement"
    chk="$(cat "$g/r1.check")"
    if [ -f "$g/r1.candidate.oscir" ]; then mkdir -p "$D/cands/mask-$MODE/$task"; cp "$g/r1.candidate.oscir" "$D/cands/mask-$MODE/$task/candidate.oscir"; fi
    IFS=$'\t' read -r _pt ct fin <<< "$(usage_of "$g/r1.resp.json")"
    m="$(jq -r '.usage.aien_constraint // {} | [(.forced // "-"), (.logits_skipped // "-"), (.allowed // "-"), (.rule_us // "-"), (.rule_us_max // "-")] | @tsv' "$g/r1.resp.json")"
  else
    chk="HTTP $code"; ct=-; fin=-; m="$(printf -- '-\t-\t-\t-\t-')"
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\tPENDING\t%s\t%s\n' "$task" "$seed" "$code" "$ct" "$fin" "$chk" "$m" "$wall" >> "$D/rows.tsv"
  echo "mask_trial: $MODE $task http=$code tokens=$ct check=$chk" >&2
done
# the sealed harness on the candidates: a dev tarball packed the way pack_dev.sh packs it
P="$D/pack"; mkdir -p "$P/src"; cp -r "$ROOT/tests/one_layer/exp445/dev/tasks" "$P/src/"
(cd "$P/src" && tar --format=ustar --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 -cf "$P/tasks.tar" tasks)
(cd "$P" && sha256sum tasks.tar > TASKS.sha256)
if OLA_ALLOW_DIRTY=1 OLA_EVIDENCE_ROOT="$D/ola" "$ROOT/tests/one_layer/acceptance/ola.sh" experiment "$P/tasks.tar" "$P/TASKS.sha256" "$D/cands" > "$D/ola.out" 2> "$D/ola.err"; then
  ev="$(sed -n 's/^ola.sh: evidence in //p' "$D/ola.err" | tail -1)"; case "$ev" in /*) ;; *) ev="$ROOT/$ev" ;; esac
  cp "$ev/sets/mask-$MODE/verdicts.txt" "$D/verdicts.txt"
  awk -F'\t' -v OFS='\t' 'NR == FNR { v[$1] = $2; next } { $7 = ($1 in v) ? v[$1] : "no_candidate"; print }' \
    <(awk '{ print $1 "\t" $2 }' "$D/verdicts.txt") "$D/rows.tsv" > "$D/rows.tsv.new" && mv "$D/rows.tsv.new" "$D/rows.tsv"
else
  echo "mask_trial: ola.sh experiment failed: $(tail -3 "$D/ola.err")" >&2
fi
cat "$D/rows.tsv"
