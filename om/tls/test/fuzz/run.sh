#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# om/tls/test/fuzz/run.sh: orchestration for the om/tls fuzz targets (fz_*.om). The targets are
# Omega; this script only builds, replays and drives AFL++ (qemu_mode, binary-only coverage).
# A trap in a target exits 100+code (code 1..14); exit >= 100 on any input is a crash, because
# om/tls/README.md requires that untrusted input never traps.
#
#   sh run.sh [STAGE...]   stages: build replay plant fuzz cmin triage service (default: build replay plant)
#
# Environment:
#   OMC          pinned compiler            (default seed/out/omc)
#   AFLPP        AFL++ v4.40c tree with aflpp-exitrange.patch and qemu_mode built (sh aflpp.sh)
#   OUT          scratch dir                (default /tmp/omtls-fuzz)
#   FUZZ_HOURS   one-shot campaign length over all targets (default 0 = skip the fuzz stage)
#   FUZZ_TARGETS targets                    (default: all eight + fz_plant for plant)
#   FUZZ_CPUS    worker cpus, at most 2     (default "10 11"; never the X925 cores 5-9, 15-19)
#   PLANT_S      blind planted-control budget, seconds (default 1800)
#   FUZZ_CORPUS / FUZZ_CRASHES / FUZZ_BOARD  service + triage locations
#   FUZZ_SLICE_S service time per target before rotating (default 3600)
set -u
D=$(cd -P "$(dirname "$0")" && pwd -P)
R=$(cd "$D/../../../.." && pwd)
OMC=${OMC:-$R/seed/out/omc}
AFLPP=${AFLPP:-$HOME/.local/lib/aflpp-4.40c}
OUT=${OUT:-/tmp/omtls-fuzz}
HOURS=${FUZZ_HOURS:-0}
TARGETS=${FUZZ_TARGETS:-fz_flight fz_hs fz_x509 fz_pem fz_dns fz_http fz_https}
CPUS=${FUZZ_CPUS:-10 11}
H=${HOME:-/home/drakestapleton}
CORPUS=${FUZZ_CORPUS:-$H/handoffs/TlsProof/fuzz/corpus}
CRASHES=${FUZZ_CRASHES:-$H/handoffs/TlsProof/crashes}
BOARD=${FUZZ_BOARD:-$H/handoffs/BOARD.md}
S=$D/seeds
NICE="nice -n 19 ionice -c3"
fails=0
say() { echo "$*"; }
bad() { echo "FAIL $*"; fails=$((fails+1)); }

for c in $CPUS; do case $c in 5|6|7|8|9|15|16|17|18|19) echo "refused: cpu $c is an X925 core" >&2; exit 2;; esac; done
[ "$(echo $CPUS | wc -w)" -le 2 ] || { echo "refused: at most 2 workers" >&2; exit 2; }

# target argument: chain-verifying targets read the fuzz trust store, fz_pem a chain file
targ() { case $1 in fz_flight|fz_hs|fz_x509) echo "$S/fzstore.pem";; fz_pem) echo "$S/chain.bin";; *) echo "";; esac; }

afl_env() {
  export AFL_PATH=$AFLPP AFL_CRASH_EXITCODE=101-114 AFL_NO_UI=1 AFL_SKIP_CPUFREQ=1 \
    AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1 AFL_AUTORESUME=1
}

build() {
  mkdir -p "$OUT/bin"
  say "omc sha256 $(sha256sum "$OMC" | cut -c1-64)"
  for t in $TARGETS fz_plant; do
    cat "$R/seed/net/http.om" "$R/seed/net/https.om" "$D/common.om" "$D/$t.om" > "$OUT/bin/$t.prog.om"
    if $NICE sh "$R/om/tls/build.sh" "$OMC" "$OUT/bin/$t.prog.om" "$OUT/bin/$t" > "$OUT/bin/$t.build.log" 2>&1
    then say "built $t $(sha256sum "$OUT/bin/$t" | cut -c1-16)"; else bad "build $t (see $OUT/bin/$t.build.log)"; fi
  done
}

# run1 TARGET FILE -> sets ex (exit status) and rc (the fz_say depth code, if any)
run1() {
  a=$(targ "$1"); timeout 20 "$OUT/bin/$1" $a < "$2" > /dev/null 2> "$OUT/err"; ex=$?
  rc=$(sed -n 's/^rc \([0-9]*\)$/\1/p' "$OUT/err" | tail -1)
}

# replay: every seed and every regression input must exit < 100; depth codes must match seeds/EXPECT
replay() {
  exp=${1:-$S/EXPECT}; n=0
  for t in $TARGETS; do
    for f in "$S/$t"/* "$D/regress/$t"/*; do
      [ -f "$f" ] || continue; run1 "$t" "$f"; n=$((n+1))
      [ "$ex" -ge 100 ] && bad "replay $t $(basename "$f") exit $ex (trap $((ex-100)))"
      [ "$ex" -eq 124 ] && bad "replay $t $(basename "$f") timeout"
      w=$(awk -v t="$t" -v s="$(basename "$f")" '$1==t && $2==s {print $3}' "$exp")
      [ -n "$w" ] && [ "$w" != "$rc" ] && bad "replay $t $(basename "$f") rc $rc expected $w"
    done
  done
  say "replayed $n inputs"
}

# plant: the planted defect must trap natively, and AFL must find it blind from \0abcd
plant() {
  printf '\0OMFZ' > "$OUT/hit"; run1 fz_plant "$OUT/hit"
  [ "$ex" -eq 103 ] && say "PASS plant native exit 103" || bad "plant native exit $ex, want 103"
  printf '\0OMFY' > "$OUT/miss"; run1 fz_plant "$OUT/miss"
  [ "$ex" -eq 0 ] && say "PASS plant near-miss exit 0" || bad "plant near-miss exit $ex, want 0"
  # negative control for replay: a corrupted expectation must FAIL
  awk 'NR==1 {$3 = ($3 == "000") ? "999" : "000"} {print}' "$S/EXPECT" > "$OUT/EXPECT.bad"
  f0=$fails; replay "$OUT/EXPECT.bad" > "$OUT/negctl.log"; f1=$fails; fails=$f0
  [ "$f1" -gt "$f0" ] && say "PASS replay negative control (corrupted EXPECT line 1 fails)" || bad "replay negative control did not fail"
  afl_env; c=$(echo $CPUS | cut -d' ' -f1); rm -rf "$OUT/plant"
  AFL_BENCH_UNTIL_CRASH=1 $NICE timeout "${PLANT_S:-1800}" "$AFLPP/afl-fuzz" -Q -c 0 -b "$c" \
    -i "$S/fz_plant" -o "$OUT/plant" -t 2000 -- "$OUT/bin/fz_plant" > "$OUT/plant.log" 2>&1
  stats fz_plant "$OUT/plant/default"
  k=$(ls "$OUT/plant/default/crashes" 2>/dev/null | grep -c '^id')
  [ "$k" -ge 1 ] && say "PASS plant found blind ($k crash)" || bad "plant not found in ${PLANT_S:-1800} s"
}

# stats TARGET DIR: one tsv line from fuzzer_stats
stats() {
  f=$2/fuzzer_stats; [ -f "$f" ] || { say "stats $1 NOT_RUN"; return; }
  g() { sed -n "s/^$1 *: //p" "$f"; }
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$1" "$(g run_time)" "$(g execs_done)" "$(g execs_per_sec)" \
    "$(g saved_crashes)" "$(g saved_hangs)" "$(g edges_found)" "$(g bitmap_cvg)" "$(g corpus_count)" | tee -a "$OUT/stats.tsv"
}

fuzz() {
  [ "$HOURS" = 0 ] && { say "fuzz NOT_RUN (FUZZ_HOURS=0)"; return; }
  afl_env; nt=$(echo $TARGETS | wc -w); nc=$(echo $CPUS | wc -w)
  secs=$(awk -v h="$HOURS" -v n="$nt" -v c="$nc" 'BEGIN{printf "%d", h*3600*c/n}')
  printf 'target\trun_time\texecs\texecs_per_sec\tcrashes\thangs\tedges\tbitmap\tcorpus\n' > "$OUT/stats.tsv"
  set -- $CPUS; i=0
  for t in $TARGETS; do
    c=$1; [ $((i % nc)) -eq 1 ] && c=${2:-$1}; i=$((i+1))
    ( $NICE "$AFLPP/afl-fuzz" -Q -c 0 -b "$c" -V "$secs" -i "$S/$t" -o "$OUT/camp/$t" -t 5000 -- \
        "$OUT/bin/$t" $(targ "$t") > "$OUT/camp-$t.log" 2>&1 ) &
    [ $((i % nc)) -eq 0 ] && wait
  done
  wait
  for t in $TARGETS; do stats "$t" "$OUT/camp/$t/default"; done
}

cmin() {
  afl_env
  for t in $TARGETS; do
    q=$OUT/camp/$t/default/queue; [ -d "$q" ] || continue
    rm -rf "$OUT/cmin/$t"; a=$(targ "$t")
    $NICE "$AFLPP/afl-cmin" -Q -T 2 -i "$q" -o "$OUT/cmin/$t" -t 5000 -- "$OUT/bin/$t" $a > "$OUT/cmin-$t.log" 2>&1
    say "cmin $t $(ls "$q" | grep -c '^id') -> $(ls "$OUT/cmin/$t" 2>/dev/null | wc -l)"
  done
}

# triage: reproduce every saved crash natively, dedupe by (target, exit, sha256), copy, one BOARD line
triage() {
  mkdir -p "$CRASHES"; new=0; tot=0
  for t in $TARGETS; do
    for f in "$OUT"/camp/"$t"/default/crashes/id* "$CORPUS"/"$t"/default/crashes/id*; do
      [ -f "$f" ] || continue; tot=$((tot+1)); run1 "$t" "$f"
      if [ "$ex" -lt 100 ] || [ "$ex" -eq 124 ]; then say "triage $t $(basename "$f") does not reproduce natively (exit $ex)"; continue; fi
      h=$(sha256sum "$f" | cut -c1-12); n=$CRASHES/$t-exit$ex-$h
      [ -e "$n" ] || { cp "$f" "$n"; new=$((new+1)); say "triage NEW $n"; }
    done
  done
  line="$(date -u +%Y-%m-%dT%H:%MZ) TlsProof.TlsFuzz: om/tls fuzz triage: $tot saved crashes, $new new unique reproduced (MEASURED $CRASHES)"
  say "$line"; [ "${FUZZ_NO_BOARD:-0}" = 1 ] || echo "$line" >> "$BOARD"
}

# service: persistent workers (one per cpu) rotating targets with AFL resume; the watchdog
# (service/fuzz-watchdog.sh) pauses/stops this unit
service() {
  afl_env; mkdir -p "$CORPUS"; set -- $TARGETS; nt=$#
  k=0
  for c in $CPUS; do
    ( j=$k; while :; do
        t=$(echo $TARGETS | cut -d' ' -f$((j % nt + 1))); j=$((j + 2))
        "$AFLPP/afl-fuzz" -Q -c 0 -b "$c" -V "${FUZZ_SLICE_S:-3600}" -i "$S/$t" -o "$CORPUS/$t" -t 5000 -- \
          "$OUT/bin/$t" $(targ "$t") >> "$CORPUS/$t.log" 2>&1
      done ) &
    k=$((k+1))
  done
  wait
}

[ $# -eq 0 ] && set -- build replay plant
for st in "$@"; do case $st in build|replay|plant|fuzz|cmin|triage|service) $st ;; *) echo "unknown stage $st" >&2; exit 2;; esac; done
say "fails=$fails"; [ $fails -eq 0 ]
