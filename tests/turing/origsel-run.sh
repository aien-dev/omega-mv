#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Drake Stapleton (AIEN / aien-dev)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# tests/turing/origsel-run.sh: omega-mv#700 ORIGSEL host glue (POSIX sh, disclosed).
# Turing-profile-ORIGSEL-v1.1. It sequences only. Its jobs:
# - verify the compiler, every pinned unit and the profile sidecar;
# - build the seeded .om evaluator, its tests, the independent C recomputer and turing-coder;
# - run the pinned aien-train V2=1 trainer where an arm needs images;
# - compute SHA-256 digests and TPS1/TSY1 trailers with coreutils sha256sum (disclosed external tool);
# - compare bytes and exit codes.
# Every number and verdict comes from origsel.om. The recomputer must reproduce the report byte for byte.
#
# MODE=qualify (default): instrument qualification on outcome-independent inputs (profile
#   verification_requirements): unit tests, SYNTHETIC fixture report and recomputation, H1 on
#   every arm and on synthetic K=5 streams (n 50, 100, 300), leakage, the full non-access audit
#   on the fixture, null fixture, refusal and mutant checks, and ORIGSEL_OWNER_RECORDS (the
#   inspected v1 owner file, never D) as a dry run when set.
# MODE=measure RECORDS=<collected file> ALLOW=<allow list> IMAGES=<dir with v2_train_mlp.img,
#   v2_ctrl_shuffled.img, v2_ctrl_untrained.img from the pinned V2=1 run on RECORDS>
#   V1="<v1 owner file> <v1 sealed file>": the measurement of profile v1.1, run once.
# Host CPU only, nice -n 19 by the caller. No GPU, network, service or model process.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MODE=${MODE:-qualify}
OUT=${OUT:-$ROOT/evidence/TURING-ORIGSEL/$MODE}
BUILD=${BUILD:-$(mktemp -d "${TMPDIR:-/tmp}/origsel.XXXXXX")}
OMC=${OMC:-$ROOT/seed/out/omc}
TRAIN=${ORIGSEL_TRAIN_GIT:-$ROOT/../aien-train}
BRIDGE=${ORIGSEL_BRIDGE_GIT:-$ROOT/../omega-bridge}
PIN=$ROOT/native/turing/origsel.pin
N=$ROOT/native/turing
PROFILE=$ROOT/calibration/profiles/Turing-profile-ORIGSEL-v1.1.toml
TRAINC=692d85104369cf0fc0bec9f35c4ca693cf746012
mkdir -p "$OUT" "$BUILD/units" "$BUILD/w"
SUM="$OUT/summary.txt"
: > "$SUM"
note() { echo "$*" | tee -a "$SUM"; }
fail() { note "ORIGSEL_RUN_FAIL $*"; exit 1; }

note "mode=$MODE"
note "source_commit=$(git -C "$ROOT" rev-parse HEAD)"
note "host=$(uname -srm)"
(cd "$ROOT/calibration/profiles" && sha256sum -c Turing-profile-ORIGSEL-v1.1.sha256) > /dev/null 2>&1 || fail "profile sidecar"
note "profile_sha256=$(sha256sum "$PROFILE" | cut -c1-64)"

# 1. Compiler and pinned units.
OMC_SHA=$(sha256sum "$OMC" | cut -c1-64)
[ "$OMC_SHA" = "$(awk '$1 == "omc" {print $2}' "$PIN")" ] || fail "compiler hash $OMC_SHA differs from the pin"
note "compiler_sha256=$OMC_SHA"
materialize() {
  LIB=""
  while read -r kind order repo commit path sha; do
    [ "$kind" = unit ] || continue
    case "$repo" in
      omega) dir=$ROOT ;; aien-train) dir=$TRAIN ;; omega-bridge) dir=$BRIDGE ;; *) return 2 ;;
    esac
    f="$BUILD/units/$order-$(basename "$path")"
    git -C "$dir" show "$commit:$path" > "$f" 2> /dev/null || return 3
    [ "$(sha256sum "$f" | cut -c1-64)" = "$sha" ] || return 4
    LIB="$LIB $f"
  done < "$1"
  [ -n "$LIB" ] || return 5
}
materialize "$PIN" || fail "pinned unit refused (rc $?)"
note "units_pinned=$(echo $LIB | wc -w)"

build() {
  name=$1; shift
  cat $LIB "$N/origsel_kappa.om" "$@" > "$BUILD/$name.om"
  "$OMC" < "$BUILD/$name.om" > "$BUILD/$name.elf" 2> "$OUT/$name.compile.stderr" || return 1
  chmod 700 "$BUILD/$name.elf"
}
build origsel "$N/origsel.om" "$N/origselmain.om" || fail "compile origsel"
build origsel-test "$N/origsel.om" "$ROOT/tests/turing/origsel-test.om" || fail "compile origsel-test"
E=$BUILD/origsel.elf
note "origsel_elf_sha256=$(sha256sum "$E" | cut -c1-64) origsel_om_sha256=$(sha256sum "$N/origsel.om" | cut -c1-64)"
make -s -C "$ROOT/tools/turing_origsel_indep" CC="${CC:-cc}" origsel_indep > "$OUT/indep.build" 2>&1 || fail "build recomputer"
IND=$ROOT/tools/turing_origsel_indep/origsel_indep
note "indep_sha256=$(sha256sum "$IND" | cut -c1-64) indep_src_sha256=$(sha256sum "$ROOT/tools/turing_origsel_indep/origsel_indep.c" | cut -c1-64)"
TCC=$(awk '$1 == "tool" && $2 == "turing-coder" {print $3}' "$PIN")
mkdir -p "$BUILD/tc" && git -C "$ROOT" archive "$TCC" src | tar -x -C "$BUILD/tc" || fail "export turing-coder $TCC"
(cd "$BUILD/tc" && ${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic -O2 -D_POSIX_C_SOURCE=200809L -Isrc -o turing-coder \
  src/turing/tc_tool.c src/turing/tc_pstream.c src/turing/tc_range.c src/turing/tc_rans.c src/turing/ty_math.c src/sha256.c \
  src/turing/tc_produce.c src/turing/ty_model.c src/turing/ty_ctr1.c -lm) > "$OUT/tc.build" 2>&1 || fail "build turing-coder"
TC=$BUILD/tc/turing-coder
note "turing_coder_commit=$TCC sha256=$(sha256sum "$TC" | cut -c1-64)"

# 2. Canonical P2 byte counts (REPORT-CONTRACT): functions and files without note and blank lines.
SELU=$BUILD/units/9-select.om; OMXU=$BUILD/units/7-omexp.om; KAP=$N/origsel_kappa.om
canon() { grep -v -e '^$' -e '^note' "$@" | wc -c | tr -d ' '; }
fnbytes() { f=$1; shift; for fn in "$@"; do awk -v n="fn $fn " 'index($0, n) == 1 {on = 1; print; next} on && (/^fn / || /^global /) {on = 0} on {print}' "$f"; done | grep -v -e '^$' -e '^note' | wc -c | tr -d ' '; }
P2SEL=$(canon "$SELU"); P2RULE=$(fnbytes "$OMXU" oxrule); P2STAT=$(fnbytes "$OMXU" oxstatic)
P2NEAR=$(fnbytes "$OMXU" oxnearok oxl1 oxnear); P2KAP=$(canon "$KAP")
note "p2_bytes select=$P2SEL oxrule=$P2RULE oxstatic=$P2STAT near=$P2NEAR kappa=$P2KAP"
PROFD=$(sha256sum "$PROFILE" | cut -c1-64)

# le64 / hexbin / stream helpers. POSIX sh has no locals: helper variables carry a _ prefix.
le64() { _n=$1; _i=0; while [ "$_i" -lt 8 ]; do _b=$((_n % 256)); _n=$((_n / 256)); printf "\\$(printf '%03o' "$_b")"; _i=$((_i + 1)); done; }
hexbin() { _h=$1; while [ -n "$_h" ]; do _b=${_h%"${_h#??}"}; _h=${_h#??}; printf "\\$(printf '%03o' "0x$_b")"; done; }
# stream MODE ARM V1 FLAGS REC ALLOW IMGDIR MODELHEX DATAHEX [CODERBLOCK]
stream() {
  printf '%s' "$1"
  for v in "$2" "$(wc -c < "$3")" "$P2SEL" "$P2RULE" "$P2STAT" "$P2NEAR" "$P2KAP" "$4"; do le64 "$v"; done
  { cat "$6"; head -c 4096 /dev/zero; } | head -c 4096
  hexbin "$PROFD"; hexbin "$8"; hexbin "$9"
  cat "$7/v2_train_mlp.img" "$7/v2_ctrl_shuffled.img" "$7/v2_ctrl_untrained.img" "$3"
  [ -n "${10:-}" ] && cat "${10}"
  cat "$5"
}
Z64=0000000000000000000000000000000000000000000000000000000000000000
ARMS="U O0 FR S R M1 M2 A CSH CUN"
# report TAG REC ALLOW IMGDIR V1: .om report, recomputer report, byte comparison.
report() {
  stream R 0 "$5" 0 "$2" "$3" "$4" $Z64 $Z64 | "$E" > "$OUT/$1.om.report" 2> "$OUT/$1.om.stderr"
  echo $? > "$OUT/$1.om.rc"
  "$IND" "$2" "$3" "$5" "$4/v2_train_mlp.img" "$4/v2_ctrl_shuffled.img" "$4/v2_ctrl_untrained.img" "$SELU" "$OMXU" "$KAP" \
    > "$OUT/$1.indep.report" 2> "$OUT/$1.indep.stderr"
  echo $? > "$OUT/$1.indep.rc"
  [ "$(cat "$OUT/$1.om.rc")" = 0 ] || return 1
  cmp -s "$OUT/$1.om.report" "$OUT/$1.indep.report" || return 2
}
# streams TAG REC ALLOW IMGDIR V1: per-arm model code, TPS1, TSY1 and both coders; writes the
# coder block TAG.coders (10 arms x rans, range x rt, ideal_ub, coded_bits).
trailer() { { printf '%s\0' "$1"; cat "$2"; } | sha256sum | cut -c1-64; }
streams() {
  W=$BUILD/w/$1; mkdir -p "$W"; : > "$OUT/$1.coders.txt"; : > "$W/coders.bin"
  DATAD=$(sha256sum "$2" | cut -c1-64)
  stream Y 0 "$5" 0 "$2" "$3" "$4" $Z64 "$DATAD" | "$E" > "$W/tsy.body" || return 1
  { cat "$W/tsy.body"; hexbin "$(trailer turing.tc.symbols.v1 "$W/tsy.body")"; } > "$W/d.tsy"
  ac=1
  for a in $ARMS; do
    stream M "$ac" "$5" 0 "$2" "$3" "$4" $Z64 $Z64 | "$E" > "$W/$a.code" || return 2
    MD=$({ printf 'turing.origsel.arm.v1\0'; cat "$W/$a.code"; } | sha256sum | cut -c1-64)
    stream P "$ac" "$5" 0 "$2" "$3" "$4" "$MD" "$DATAD" | "$E" > "$W/$a.body" || return 3
    { cat "$W/$a.body"; hexbin "$(trailer turing.tc.pstream.v1 "$W/$a.body")"; } > "$W/$a.tps"
    for c in rans range; do
      rt=0
      "$TC" encode "$c" "$W/$a.tps" "$W/d.tsy" "$W/$a.$c" > /dev/null 2>> "$OUT/$1.coders.stderr" \
        && "$TC" verify "$c" "$W/$a.tps" "$W/d.tsy" "$W/$a.$c" > "$W/$a.$c.verify" 2>> "$OUT/$1.coders.stderr" && rt=1
      iu=$(sed -n 's/^ideal_ub=//p' "$W/$a.$c.verify" 2> /dev/null); iu=${iu:-0}
      cb=$(sed -n 's/^coded_bits=//p' "$W/$a.$c.verify" 2> /dev/null); cb=${cb:-0}
      echo "$a $c rt=$rt ideal_ub=$iu coded_bits=$cb model_digest=$MD" >> "$OUT/$1.coders.txt"
      { le64 "$rt"; le64 "$iu"; le64 "$cb"; } >> "$W/coders.bin"
    done
    ac=$((ac + 1))
  done
  stream H 0 "$5" 1 "$2" "$3" "$4" $Z64 $Z64 "$W/coders.bin" | "$E" > "$OUT/$1.h.report" || return 4
  (cd "$W" && sha256sum -- *.tps d.tsy *.code) > "$OUT/$1.streams.sha256"
}
# retrain TAG REC ALLOW: the pinned aien-train V2=1 run (trainer and controls) on REC.
retrain() {
  # The trainer's glue reads pinned objects with git show, so it needs a clone at the pin.
  [ -d "$BUILD/train" ] || { git clone -q --no-hardlinks "$TRAIN" "$BUILD/train" && git -C "$BUILD/train" checkout -q "$TRAINC"; } || return 1
  [ "$(git -C "$BUILD/train" rev-parse HEAD)" = "$TRAINC" ] || return 1
  V2=1 RECORDS="$2" ALLOW="$3" OUT="$OUT/train-$1" BUILD="$BUILD/train-$1" sh "$BUILD/train/tests/si19/run-select.sh" \
    > "$OUT/train-$1.log" 2>&1 || return 2
  grep -q SI19_SELECT_V2_RUN_PASS "$OUT/train-$1.log" || return 3
}
rows() { awk '$1 == "arm" {for (i = 1; i <= NF; i++) if ($i ~ /^rows=/) print $2, $i}' "$1"; }
recbody() { n=$(sed -n 's/^d n=\([0-9]*\).*/\1/p' "$OUT/$1.om.report"); tail -c +129 "$BUILD/w/$1/$2.tps" | head -c $((34 * n)); }
# audit TAG REC ALLOW IMGDIR V1: the program-enforced non-access audit (profile blinding_mechanism 2).
# Returns 0 PASS; 1..9 the audit could not run or C' does not reproduce admission (BLOCKED);
# 11..19 a predictive artifact changed with the held-out outcomes (INVALID).
audit() {
  t=$1
  stream A 0 "$5" 0 "$2" "$3" "$4" $Z64 $Z64 | "$E" > "$BUILD/$t.cprime" || return 1
  CF=$("$IND" --fnv "$BUILD/$t.cprime")
  { printf 'F'; cat "$BUILD/$t.cprime"; } | "$E" > "$BUILD/$t.cprime.fnv" || return 1
  printf '%s SYNTHETIC orig-turing-audit\n' "$CF" > "$BUILD/$t.cprime.allow"
  retrain "$t-cprime" "$BUILD/$t.cprime" "$BUILD/$t.cprime.allow" || return 2
  CI=$BUILD/train-$t-cprime
  report "$t-cprime" "$BUILD/$t.cprime" "$BUILD/$t.cprime.allow" "$CI" "$5" || return 4
  [ "$(sed -n 2,3p "$OUT/$t.om.report")" = "$(sed -n 2,3p "$OUT/$t-cprime.om.report")" ] || return 8
  for img in v2_train_mlp v2_ctrl_shuffled v2_ctrl_untrained; do
    { printf 'K'; cat "$4/$img.img" "$CI/$img.img" "$BUILD/$t.cprime.fnv"; } | "$E" > "$OUT/$t.audit-$img.txt"
    grep -q '^K same$' "$OUT/$t.audit-$img.txt" || { grep -q '^K diff' "$OUT/$t.audit-$img.txt" && return 13; return 3; }
  done
  rows "$OUT/$t.om.report" > "$OUT/$t.rows"; rows "$OUT/$t-cprime.om.report" > "$OUT/$t-cprime.rows"
  cmp -s "$OUT/$t.rows" "$OUT/$t-cprime.rows" || return 15
  streams "$t-cprime" "$BUILD/$t.cprime" "$BUILD/$t.cprime.allow" "$CI" "$5" || return 6
  for a in $ARMS; do
    [ "$(recbody "$t" "$a" | sha256sum)" = "$(recbody "$t-cprime" "$a" | sha256sum)" ] || return 17
  done
}
# idealbad TAG: a round-tripped stream whose turing-coder ideal_ub differs from the .om sum.
idealbad() { grep -q '^h1 .* rt=1 ideal=mismatch' "$OUT/$1.h.report"; }

if [ "$MODE" = qualify ]; then
  "$BUILD/origsel-test.elf" > "$OUT/unit-test.stdout"; [ $? = 0 ] || fail "unit tests $(cat "$OUT/unit-test.stdout")"
  note "unit_tests $(cat "$OUT/unit-test.stdout")"
  "$IND" --selftest > "$OUT/indep-selftest.stdout" 2>&1 || fail "recomputer selftest"
  : > "$BUILD/empty"
  # Fixture: the pinned trainer generates the SYNTHETIC fixture (seed 4242, 400 records) and trains.
  retrain fix "" "" || fail "trainer on fixture"
  FIXB=$BUILD/train-fix; FREC=$FIXB/fixture.rb; FALLOW=$FIXB/allow.txt
  report fix "$FREC" "$FALLOW" "$FIXB" "$BUILD/empty" || fail "fixture report (rc $?)"
  note "fixture_report om=indep identical sha256=$(sha256sum "$OUT/fix.om.report" | cut -c1-64)"
  streams fix "$FREC" "$FALLOW" "$FIXB" "$BUILD/empty" || fail "fixture streams (rc $?)"
  idealbad fix && fail "ideal_ub mismatch between .om and turing-coder"
  [ "$(grep -c '^h1 .* rt=1 ideal=match' "$OUT/fix.h.report")" = 20 ] || fail "fixture round trips"
  note "fixture_h1 roundtrip=20/20 ideal_match=20/20 band_ok=$(grep -c '^h1 .*band=ok' "$OUT/fix.h.report")/20"
  # The F mode (64-bit FNV as bytes) on a value at or above 2^63; od reads it back.
  FB=$({ printf 'F'; cat "$FREC"; } | "$E" | od -An -tu8 | tr -d ' ')
  [ "$FB" = "$("$IND" --fnv "$FREC")" ] || fail "F mode $FB differs from the recomputer's FNV"
  note "fnv_bytes PASS ($FB)"
  # Burned exclusion and D_fresh / D_v1fam: V1 = the fixture's first 100 records.
  head -c 25600 "$FREC" > "$BUILD/v1part"
  report fixv1 "$FREC" "$FALLOW" "$FIXB" "$BUILD/v1part" || fail "burned-path report (rc $?)"
  note "burned_path om=indep identical $(sed -n 3p "$OUT/fixv1.om.report")"
  # Synthetic K=5 coder streams (no record involved): n 50, 100, 300.
  for n in 50 100 300; do
    stream Q "$n" "$BUILD/empty" 0 "$FREC" "$FALLOW" "$FIXB" $Z64 $Z64 | "$E" > "$BUILD/q$n.body" || fail "synthetic Q $n"
    stream W "$n" "$BUILD/empty" 0 "$FREC" "$FALLOW" "$FIXB" $Z64 $Z64 | "$E" > "$BUILD/w$n.body" || fail "synthetic W $n"
    { cat "$BUILD/q$n.body"; hexbin "$(trailer turing.tc.pstream.v1 "$BUILD/q$n.body")"; } > "$BUILD/q$n.tps"
    { cat "$BUILD/w$n.body"; hexbin "$(trailer turing.tc.symbols.v1 "$BUILD/w$n.body")"; } > "$BUILD/w$n.tsy"
    for c in rans range; do
      rt=0
      "$TC" encode "$c" "$BUILD/q$n.tps" "$BUILD/w$n.tsy" "$BUILD/q$n.$c" > /dev/null 2>&1 \
        && "$TC" verify "$c" "$BUILD/q$n.tps" "$BUILD/w$n.tsy" "$BUILD/q$n.$c" > "$BUILD/q$n.$c.v" 2>&1 && rt=1
      iu=$(sed -n 's/^ideal_ub=//p' "$BUILD/q$n.$c.v"); cb=$(sed -n 's/^coded_bits=//p' "$BUILD/q$n.$c.v")
      res=$({ printf 'B'; for v in "$n" "${iu:-0}" "${cb:-0}" "$rt" 0 0 0 0; do le64 "$v"; done; } | "$E")
      note "band_synthetic n=$n coder=$c rt=$rt ideal_ub=${iu:-0} coded_bits=${cb:-0}$res"
      [ "$c" = rans ] && { echo "$res" | grep -q 'band=ok' || fail "rans band at n=$n"; }
    done
  done
  # Leakage: C' records under the same images must leave every TPS1 record byte and row unchanged.
  stream A 0 "$BUILD/empty" 0 "$FREC" "$FALLOW" "$FIXB" $Z64 $Z64 | "$E" > "$BUILD/leak.rec" || fail "leak derivative"
  CFL=$("$IND" --fnv "$BUILD/leak.rec"); printf '%s SYNTHETIC leak\n' "$CFL" > "$BUILD/leak.allow"
  report leak "$BUILD/leak.rec" "$BUILD/leak.allow" "$FIXB" "$BUILD/empty" || fail "leak report (rc $?)"
  streams leak "$BUILD/leak.rec" "$BUILD/leak.allow" "$FIXB" "$BUILD/empty" || fail "leak streams"
  rows "$OUT/fix.om.report" > "$OUT/fix.rows"; rows "$OUT/leak.om.report" > "$OUT/leak.rows"
  cmp -s "$OUT/fix.rows" "$OUT/leak.rows" || fail "leakage: rows changed"
  for a in $ARMS; do [ "$(recbody fix "$a" | sha256sum)" = "$(recbody leak "$a" | sha256sum)" ] || fail "leakage: TPS1 records of $a changed"; done
  cmp -s "$BUILD/w/fix/d.tsy" "$BUILD/w/leak/d.tsy" && fail "leakage test inert: labels did not change"
  note "leakage PASS (labels changed, 10/10 TPS1 record bodies and every row identical)"
  # Full non-access audit on the fixture (retraining on C').
  audit fix "$FREC" "$FALLOW" "$FIXB" "$BUILD/empty" || fail "audit on fixture (rc $?)"
  note "audit_fixture PASS ($(cat "$OUT"/fix.audit-*.txt | tr '\n' ' '))"
  # Null fixture: state-independent labels everywhere; A must not beat O0.
  stream N 0 "$BUILD/empty" 0 "$FREC" "$FALLOW" "$FIXB" $Z64 $Z64 | "$E" > "$BUILD/null.rec" || fail "null derivative"
  CFN=$("$IND" --fnv "$BUILD/null.rec"); printf '%s SYNTHETIC null\n' "$CFN" > "$BUILD/null.allow"
  retrain null "$BUILD/null.rec" "$BUILD/null.allow" || fail "trainer on null fixture"
  report null "$BUILD/null.rec" "$BUILD/null.allow" "$BUILD/train-null" "$BUILD/empty" || fail "null report (rc $?)"
  UB=$(awk '$1 == "t" && $2 == "A" && $3 == "O0" {sub("ub=", "", $6); print $6}' "$OUT/null.om.report")
  [ "$UB" -le 0 ] || fail "null fixture: ub(A;O0) = $UB > 0"
  note "null_fixture PASS ub(A;O0)=$UB"
  # Refusals and mutants.
  sed 's/^\(unit 9 .* \)[0-9a-f]\{64\}$/\1'"$Z64"'/' "$PIN" > "$BUILD/bad.pin"
  ( materialize "$BUILD/bad.pin" ) && fail "unpinned unit accepted"
  note "refuse_unpinned_unit PASS"
  awk '$1 == "arm" && $2 == "M1" {sub(/p1=[0-9]+/, "p1=1")} {print}' "$OUT/fix.om.report" > "$BUILD/forged-l.report"
  cmp -s "$BUILD/forged-l.report" "$OUT/fix.indep.report" && fail "wrong L(M|G) receipt accepted"
  awk '$1 == "t" && $2 == "A" && $3 == "FR" {sub(/lb=-?[0-9]+/, "lb=1")} {print}' "$OUT/fix.om.report" > "$BUILD/forged-t.report"
  cmp -s "$BUILD/forged-t.report" "$OUT/fix.indep.report" && fail "tampered receipt accepted"
  note "refuse_wrong_model_cost PASS refuse_tampered_receipt PASS"
  NREC=$(($(wc -c < "$FREC") / 256))
  { cat "$FREC"; tail -c 256 "$FREC" | head -c 112; le64 1099511627776; tail -c 136 "$FREC"; } > "$BUILD/dup.rec"
  CFD=$("$IND" --fnv "$BUILD/dup.rec"); printf '%s SYNTHETIC dup\n' "$CFD" > "$BUILD/dup.allow"
  report dup "$BUILD/dup.rec" "$BUILD/dup.allow" "$FIXB" "$BUILD/empty" || fail "duplicate report (rc $?)"
  [ "$(sed -n 2p "$OUT/dup.om.report" | sed 's/^admit records=[0-9]* //')" = "$(sed -n 2p "$OUT/fix.om.report" | sed 's/^admit records=[0-9]* //')" ] \
    || fail "duplicate record admitted"
  note "refuse_duplicate PASS (records $NREC -> $((NREC + 1)), admitted unchanged)"
  { head -c 96 "$FREC"; le64 15; tail -c +105 "$FREC"; } > "$BUILD/unp.rec"
  CFU=$("$IND" --fnv "$BUILD/unp.rec"); printf '%s SYNTHETIC unpaired\n' "$CFU" > "$BUILD/unp.allow"
  report unp "$BUILD/unp.rec" "$BUILD/unp.allow" "$FIXB" "$BUILD/empty" || fail "unpaired report (rc $?)"
  A0=$(sed -n 's/.* admitted=\([0-9]*\).*/\1/p' "$OUT/fix.om.report"); A1=$(sed -n 's/.* admitted=\([0-9]*\).*/\1/p' "$OUT/unp.om.report")
  [ "$A1" -lt "$A0" ] || fail "unpaired record admitted"
  note "refuse_unpaired PASS (admitted $A0 -> $A1)"
  # Evaluator mutants: each must fail the in-process tests or make the .om report differ from
  # the recomputer; a mutant that does not compile is a harness error, not a kill.
  mutant() {
    sed "$2" "$N/origsel.om" > "$BUILD/mut$1.om"
    cmp -s "$BUILD/mut$1.om" "$N/origsel.om" && fail "mutant $1 pattern matched nothing"
    cat $LIB "$N/origsel_kappa.om" "$BUILD/mut$1.om" "$ROOT/tests/turing/origsel-test.om" > "$BUILD/mutt$1.om"
    "$OMC" < "$BUILD/mutt$1.om" > "$BUILD/mutt$1.elf" 2> /dev/null || fail "mutant $1 test unit does not compile"
    chmod 700 "$BUILD/mutt$1.elf"
    "$BUILD/mutt$1.elf" > /dev/null 2>&1 || { note "mutant $1 killed by unit tests"; return 0; }
    cat $LIB "$N/origsel_kappa.om" "$BUILD/mut$1.om" "$N/origselmain.om" > "$BUILD/mutu$1.om"
    "$OMC" < "$BUILD/mutu$1.om" > "$BUILD/mut$1.elf" 2> /dev/null || fail "mutant $1 does not compile"
    chmod 700 "$BUILD/mut$1.elf"
    stream R 0 "$BUILD/empty" 0 "$3" "$4" "$FIXB" $Z64 $Z64 | "$BUILD/mut$1.elf" > "$BUILD/mut$1.report" 2> /dev/null
    cmp -s "$BUILD/mut$1.report" "$OUT/$5.indep.report" && return 1
    note "mutant $1 killed by recomputation"
    return 0
  }
  mutant 1 's/^if lt get k a get k b$/if le get k a get k b/' "$FREC" "$FALLOW" fix || fail "mutant 1 (tie rule) survived"
  mutant 2 's/^put TQS i sub sub va vb d$/put TQS i sub va vb/' "$FREC" "$FALLOW" fix || fail "mutant 2 (model cost) survived"
  mutant 3 's/^put TQT 91 get TQS 166$/put TQT 91 get TQS 499/' "$FREC" "$FALLOW" fix || fail "mutant 3 (bound rank) survived"
  mutant 4 's/^if ne get TQB c 0$/if eq get TQB c 0/' "$FREC" "$FALLOW" fix || fail "mutant 4 (blocks) survived"
  mutant 5 '/^fn tqmlp /,/^fn tqctx /s/^ret tqord5 k$/ret tqqord r/' "$FREC" "$FALLOW" fix || fail "mutant 5 (leaky MLP order) survived"
  mutant 6 's/^tqgam add n 1$/tqgam add n 2/' "$FREC" "$FALLOW" fix || fail "mutant 6 (row code length) survived"
  note "evaluator_mutants 6/6 killed"
  if [ -n "${ORIGSEL_OWNER_RECORDS:-}" ]; then
    retrain own "$ORIGSEL_OWNER_RECORDS" "$BUILD/train/docs/si19/orig1-datasets.txt" || fail "trainer on owner file"
    report own "$ORIGSEL_OWNER_RECORDS" "$BUILD/train/docs/si19/orig1-datasets.txt" "$BUILD/train-own" "$BUILD/empty" || fail "owner dry run (rc $?)"
    note "owner_dry_run om=indep identical (inspected v1 file, development only, never D) $(grep '^experiment' "$OUT/own.om.report")"
    rm -rf "$OUT/train-own"
  fi
  (cd "$OUT" && sha256sum -- *.report *.txt *.rc 2> /dev/null) > "$OUT/raw.sha256"
  note "ORIGSEL_QUALIFY_PASS"
  exit 0
fi

if [ "$MODE" = measure ]; then
  blocked() { note "ORIGSEL_VERDICT BLOCKED ($*)"; exit 3; }
  invalid() { note "ORIGSEL_VERDICT INVALID ($*)"; exit 2; }
  [ -f "${RECORDS:-}" ] && [ -f "${ALLOW:-}" ] || blocked "RECORDS and ALLOW required"
  # E's qualification receipts must be PASS for exactly this evaluator and recomputer.
  QS=$ROOT/evidence/TURING-ORIGSEL/qualify/summary.txt
  [ "$(tail -n 1 "$QS" 2> /dev/null)" = ORIGSEL_QUALIFY_PASS ] || blocked "qualification receipt not PASS"
  grep -q "origsel_om_sha256=$(sha256sum "$N/origsel.om" | cut -c1-64)" "$QS" || blocked "evaluator differs from the qualified one"
  grep -q "indep_src_sha256=$(sha256sum "$ROOT/tools/turing_origsel_indep/origsel_indep.c" | cut -c1-64)" "$QS" || blocked "recomputer differs from the qualified one"
  grep -q "origsel_elf_sha256=$(sha256sum "$E" | cut -c1-64)" "$QS" || blocked "evaluator binary differs from the qualified one"
  # V1 = exactly the two pinned v1 files, in order (profile burned_exclusion).
  set -- ${V1:-}
  [ "$#" = 2 ] || blocked "V1 must name the v1 owner file and the v1 sealed file"
  [ "$("$IND" --fnv "$1")" = 16622604375676129865 ] || blocked "V1 owner file fnv"
  [ "$("$IND" --fnv "$2")" = 12602815733741495377 ] || blocked "V1 sealed file fnv"
  cat "$1" "$2" > "$BUILD/v1.bin" || blocked "V1 read"
  if [ -z "${IMAGES:-}" ]; then
    retrain d "$RECORDS" "$ALLOW" || blocked "pinned V2=1 trainer on RECORDS"
    IMAGES=$BUILD/train-d
    note "images: pinned V2=1 run by this glue (aien-train $TRAINC)"
  fi
  note "records_sha256=$(sha256sum "$RECORDS" | cut -c1-64) records_fnv=$("$IND" --fnv "$RECORDS")"
  for img in v2_train_mlp v2_ctrl_shuffled v2_ctrl_untrained; do note "image $img sha256=$(sha256sum "$IMAGES/$img.img" | cut -c1-64)"; done
  note "v1_sha256=$(sha256sum "$BUILD/v1.bin" | cut -c1-64)"
  report d "$RECORDS" "$ALLOW" "$IMAGES" "$BUILD/v1.bin"
  rc=$?
  [ "$rc" = 2 ] && invalid "the independent recomputation disagrees with the evaluator"
  [ "$rc" = 0 ] || blocked "evaluator refusal or instrument failure, rc $(cat "$OUT/d.om.rc"): $(head -c 200 "$OUT/d.om.report")"
  note "report om=indep identical sha256=$(sha256sum "$OUT/d.om.report" | cut -c1-64)"
  streams d "$RECORDS" "$ALLOW" "$IMAGES" "$BUILD/v1.bin" || blocked "stream writer failure (rc $?)"
  idealbad d && invalid "turing-coder ideal_ub differs from the evaluator on a round-tripped stream"
  note "h1 $(grep -c '^h1 .*rt=1 ideal=match.*band=ok' "$OUT/d.h.report")/20 arm x coder round trip + ideal + band"
  audit d "$RECORDS" "$ALLOW" "$IMAGES" "$BUILD/v1.bin"
  rc=$?
  [ "$rc" -ge 11 ] && invalid "non-access audit failed (rc $rc)"
  [ "$rc" = 0 ] || blocked "non-access audit could not run or C' admission differs (rc $rc)"
  note "non_access_audit PASS"
  rm -rf "$OUT/train-d-cprime"
  (cd "$OUT" && sha256sum -- *.report *.txt *.rc 2> /dev/null) > "$OUT/raw.sha256"
  note "$(grep '^experiment' "$OUT/d.om.report")"
  note "ORIGSEL_VERDICT $(sed -n 's/^experiment //p' "$OUT/d.om.report")"
  exit 0
fi
fail "unknown MODE $MODE"
