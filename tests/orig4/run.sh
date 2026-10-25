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

# tests/orig4/run.sh: omega-mv#731 ORIG-4 host glue (POSIX sh, disclosed). It only sequences:
# verify the seeded compiler and every pinned source against native/orig4/orig4.pin,
# concatenate, compile, generate the SYNTHETIC fixture, run the in-process tests, run the
# shadow advisor over the fixture with both candidate images, the kill switch, the
# process-path refusals and the budget, the static default-off proof, and record every
# status under OUT. Every judgment about records, images and scores is made by the .om
# units; this file compares exit codes, line shapes and counts.
# Host CPU only. No GPU, network, service or model process is touched.
# ORIG4_RECORDS=<omexp-v1 file> ORIG4_ALLOW=<allow-list file>: real-record mode, the advisor
# runs over that file (owner records stay local; point OUT outside the tree).
# ORIG4_OVERHEAD=1 with ORIG4_RECORDS, ORIG4_ALLOW and ORIG4_IMAGE (a v2 MLP image): the
# per-decision overhead runs of benchmarks-mv docs/orig3/PROTOCOL-v2.md, v2 admission,
# interleaved ORIG4_REPS (default 5) times: disabled, image scorer, retrieval. Wall times in
# microseconds land in OUT/<run>.wall_us; the judge computes the overhead from them.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${OUT:-$ROOT/evidence/ORIG4-SHADOW/run}
BUILD=${BUILD:-$(mktemp -d "${TMPDIR:-/tmp}/orig4.XXXXXX")}
OMC=$ROOT/seed/out/omc
TRAIN=${ORIG4_TRAIN_GIT:-$ROOT/../aien-train}
BRIDGE=${ORIG4_BRIDGE_GIT:-$ROOT/../omega-bridge}
PIN=$ROOT/native/orig4/orig4.pin
CAND=$ROOT/native/orig4/candidates
N=$ROOT/native/orig4
FIXSEED=7
FIXCOUNT=780
FIXFNV=626048380840256264
FIX100FNV=3415637669635212362
MLP=$CAND/mlp-scratch-seed19024.img
AFF=$CAND/affine-seed19024.img
mkdir -p "$OUT" "$BUILD/units"
SUM="$OUT/summary.txt"
: > "$SUM"
note() { echo "$*" | tee -a "$SUM"; }
fail() { note "ORIG4_SHADOW_RUN_FAIL $*"; exit 1; }
ms() { date +%s%N; }

note "build_dir=$BUILD"
note "source_commit=$(git -C "$ROOT" rev-parse HEAD)"
note "host=$(uname -srm)"

# 1. The compiler: seed/out/omc must match seed/out/omc.sha256 and the pin.
set -- $(sha256sum "$OMC")
OMC_SHA=$1
[ "$OMC_SHA" = "$(awk '{print $1}' "$ROOT/seed/out/omc.sha256")" ] || fail "compiler hash $OMC_SHA differs from seed/out/omc.sha256"
[ "$OMC_SHA" = "$(awk '$1 == "omc" {print $2}' "$PIN")" ] || fail "compiler hash $OMC_SHA differs from orig4.pin"
note "compiler_sha256=$OMC_SHA"

# 2. The pinned units: git show from the pinned object stores, hash against the pin.
LIB=""
while read -r kind order repo commit path sha; do
  [ "$kind" = "unit" ] || continue
  case "$repo" in
    omega) dir=$ROOT ;;
    aien-train) dir=$TRAIN ;;
    omega-bridge) dir=$BRIDGE ;;
    *) fail "unknown pin repo $repo" ;;
  esac
  f="$BUILD/units/$order-$(basename "$path")"
  git -C "$dir" show "$commit:$path" > "$f" 2>> "$OUT/materialize.stderr" || fail "materialize $repo $commit:$path"
  set -- $(sha256sum "$f")
  [ "$1" = "$sha" ] || fail "unit $path hash $1 differs from the pin $sha"
  LIB="$LIB $f"
done < "$PIN"
[ -n "$LIB" ] || fail "no units in the pin"
note "units_pinned=$(echo $LIB | wc -w)"
(cd "$CAND" && sha256sum -c SHA256SUMS) > "$OUT/candidates-check.txt" 2>&1 || fail candidate hashes
awk '$1 == "image" {print $2 "  " $3}' "$PIN" | while read -r img sha; do
  set -- $(sha256sum "$CAND/$img")
  [ "$1" = "$sha" ] || exit 1
done || fail "candidate image differs from the pin"

# build NAME CALLER...: one concatenated unit compiled by the seeded compiler.
build() {
  name=$1; shift
  cat $LIB "$@" > "$BUILD/$name.om"
  "$OMC" < "$BUILD/$name.om" > "$BUILD/$name.elf" 2> "$OUT/$name.compile.stderr"
  rc=$?
  echo "$rc" > "$OUT/$name.compile.rc"
  [ "$rc" -eq 0 ] || return "$rc"
  head -c 4 "$BUILD/$name.elf" | od -An -c | grep -q 'E   L   F' || return 70
  chmod 700 "$BUILD/$name.elf"
}
build shadow "$N/shadow.om" "$N/shadowmain.om" || fail compile shadow
build shadow-test "$N/shadow.om" "$ROOT/tests/orig4/shadow-test.om" || fail compile shadow-test
SH="$BUILD/shadow.elf"
note "binaries_sha256 shadow=$(sha256sum "$SH" | cut -c1-16) shadow-test=$(sha256sum "$BUILD/shadow-test.elf" | cut -c1-16)"

# le64 N: 8 little-endian bytes. cfg W...: config words. allowblock FILE: the 4096-byte block.
le64() { n=$1; i=0; while [ "$i" -lt 8 ]; do b=$((n % 256)); n=$((n / 256)); printf "\\$(printf '%03o' "$b")"; i=$((i + 1)); done; }
cfg() { for v in "$@"; do le64 "$v"; done; }
allowblock() { { cat "$1"; head -c 4096 /dev/zero; } | head -c 4096; }
count() { grep -c -E "$1" "$OUT/$2.stdout"; }

# run NAME ENABLE BUDGET IMGLEN IMAGE RECORDS ALLOWFILE [ADMISSION KIND]: one advisor run
# (mode S); the exit code is returned, stdout/stderr/rc and the wall time land in OUT.
run() {
  name=$1
  t0=$(ms)
  { printf 'S'; cfg "$2" "$3" "$4" "${8:-0}" "${9:-0}" 0 0 0; allowblock "$7"; cat "$5"; cat "$6"; } \
    | "$SH" > "$OUT/$name.stdout" 2> "$OUT/$name.stderr"
  rc=$?; t1=$(ms)
  echo "$rc" > "$OUT/$name.rc"
  echo "$(( (t1 - t0) / 1000000 ))" > "$OUT/$name.wall_ms"
  echo "$(( (t1 - t0) / 1000 ))" > "$OUT/$name.wall_us"
  note "$name rc=$rc wall_ms=$(cat "$OUT/$name.wall_ms") lines=$(wc -l < "$OUT/$name.stdout") $(grep -E '^(COST|FALLBACK all|SHADOW disabled)' "$OUT/$name.stdout" | tail -n 1)"
  return "$rc"
}
# shape NAME: every stdout line of a run is SHADOW, ADVICE, FALLBACK or COST.
shape() {
  if grep -v -E '^(SHADOW|ADVICE|FALLBACK|COST) ' "$OUT/$1.stdout" | grep -q .; then
    note "$1: unexpected output line: $(grep -v -E '^(SHADOW|ADVICE|FALLBACK|COST) ' "$OUT/$1.stdout" | head -n 1)"
    return 1
  fi
}
# expect NAME RC: the run's exit code.
expect() { [ "$(cat "$OUT/$1.rc")" -eq "$2" ] || fail "$1 exit $(cat "$OUT/$1.rc"), expected $2"; }
# hash FILE: the records' FNV-1a 64 through the advisor's H mode.
hash() { { printf 'H'; cat "$1"; } | "$SH"; }

if [ "${ORIG4_OVERHEAD:-0}" = 1 ]; then
  [ -f "${ORIG4_RECORDS:-}" ] || fail "ORIG4_RECORDS ${ORIG4_RECORDS:-unset}"
  [ -f "${ORIG4_ALLOW:-}" ] || fail "ORIG4_ALLOW ${ORIG4_ALLOW:-unset}"
  [ -f "${ORIG4_IMAGE:-}" ] || fail "ORIG4_IMAGE ${ORIG4_IMAGE:-unset}"
  note "overhead records fnv=$(hash "$ORIG4_RECORDS") bytes=$(wc -c < "$ORIG4_RECORDS") image_sha256=$(sha256sum "$ORIG4_IMAGE" | cut -c1-64)"
  i=1
  while [ "$i" -le "${ORIG4_REPS:-5}" ]; do
    run "ovh_disabled_$i" 0 2048 11560 "$ORIG4_IMAGE" "$ORIG4_RECORDS" "$ORIG4_ALLOW" 1 0; expect "ovh_disabled_$i" 0
    run "ovh_mlp_$i" 1 2048 11560 "$ORIG4_IMAGE" "$ORIG4_RECORDS" "$ORIG4_ALLOW" 1 0; expect "ovh_mlp_$i" 0
    shape "ovh_mlp_$i" || fail "ovh_mlp_$i shape"
    grep -q '^SHADOW admission=v2 admitted=[0-9]* advisor=0$' "$OUT/ovh_mlp_$i.stdout" || fail "ovh_mlp_$i admission line"
    run "ovh_near_$i" 1 2048 11560 "$ORIG4_IMAGE" "$ORIG4_RECORDS" "$ORIG4_ALLOW" 1 1; expect "ovh_near_$i" 0
    shape "ovh_near_$i" || fail "ovh_near_$i shape"
    grep -q '^SHADOW admission=v2 admitted=[0-9]* advisor=1$' "$OUT/ovh_near_$i.stdout" || fail "ovh_near_$i admission line"
    for k in disabled mlp near; do note "OVERHEAD run=$k rep=$i wall_us=$(cat "$OUT/ovh_${k}_$i.wall_us")"; done
    i=$((i + 1))
  done
  (cd "$OUT" && sha256sum -- *.stdout *.rc *.compile.stderr *.wall_us) > "$OUT/raw.sha256"
  note "ORIG4_SHADOW_OVERHEAD_PASS"
  exit 0
fi

if [ -n "${ORIG4_RECORDS:-}" ]; then
  # Real-record mode: the given records, the given allow-list, both candidates; nothing else.
  [ -f "$ORIG4_RECORDS" ] || fail "ORIG4_RECORDS $ORIG4_RECORDS"
  [ -f "${ORIG4_ALLOW:-}" ] || fail "ORIG4_ALLOW ${ORIG4_ALLOW:-unset}"
  note "records_source=owner-records fnv=$(hash "$ORIG4_RECORDS") bytes=$(wc -c < "$ORIG4_RECORDS") (real-record run; development-only; bytes stay local)"
  run owner_mlp 1 2048 11560 "$MLP" "$ORIG4_RECORDS" "$ORIG4_ALLOW"; expect owner_mlp 0; shape owner_mlp || fail owner_mlp shape
  run owner_affine 1 2048 1816 "$AFF" "$ORIG4_RECORDS" "$ORIG4_ALLOW"; expect owner_affine 0; shape owner_affine || fail owner_affine shape
  note "COST_WALL records=$(sed -n 's/^SHADOW records=\([0-9]*\) .*/\1/p' "$OUT/owner_mlp.stdout") ms=$(cat "$OUT/owner_mlp.wall_ms") image=mlp owner-records"
  note "COST_WALL records=$(sed -n 's/^SHADOW records=\([0-9]*\) .*/\1/p' "$OUT/owner_affine.stdout") ms=$(cat "$OUT/owner_affine.wall_ms") image=affine owner-records"
  (cd "$OUT" && sha256sum -- *.stdout *.rc *.compile.stderr) > "$OUT/raw.sha256"
  note "ORIG4_SHADOW_RUN_PASS (owner-records)"
  exit 0
fi

# 3. The SYNTHETIC fixture (sxgen seed 7, 780 records) through the advisor's G mode, and the
# allow-list of docs/si19/orig1-datasets.txt entries for it (aien-train, hashes only).
{ printf 'G'; cfg "$FIXSEED" "$FIXCOUNT"; } | "$SH" > "$BUILD/fixture.rb" 2> "$OUT/fixture.stderr"
rc=$?; echo "$rc" > "$OUT/fixture.rc"
[ "$rc" -eq 0 ] || fail fixture generation
[ "$(wc -c < "$BUILD/fixture.rb")" -eq $((FIXCOUNT * 256)) ] || fail fixture size
FNV=$(hash "$BUILD/fixture.rb")
[ "$FNV" = "$FIXFNV" ] || fail "fixture fnv $FNV, expected $FIXFNV"
printf '%s SYNTHETIC fixture-seed7-780-records\n%s SYNTHETIC fixture-seed7-100-records\n' "$FIXFNV" "$FIX100FNV" > "$BUILD/allow.txt"
note "records_source=SYNTHETIC fixture seed=$FIXSEED count=$FIXCOUNT fnv=$FNV sha256=$(sha256sum "$BUILD/fixture.rb" | cut -d' ' -f1)"

# 4. In-process tests (the candidate MLP image on stdin).
t0=$(ms)
"$BUILD/shadow-test.elf" < "$MLP" > "$OUT/shadow-test.stdout" 2> "$OUT/shadow-test.stderr"
rc=$?; t1=$(ms)
echo "$rc" > "$OUT/shadow-test.rc"
note "shadow-test rc=$rc wall_ms=$(( (t1 - t0) / 1000000 )) $(cat "$OUT/shadow-test.stdout")"
[ "$rc" -eq 0 ] || fail shadow-test
grep -q '^ORIG4_SHADOW_TEST_PASS checks=[1-9]' "$OUT/shadow-test.stdout" || fail shadow-test verdict line

# 5. The advisor over the fixture with both candidates (config: enable 1, budget 2048).
run fixture_mlp 1 2048 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow.txt"; expect fixture_mlp 0
shape fixture_mlp || fail fixture_mlp shape
grep -q "^SHADOW records=$FIXCOUNT data_fnv=$FIXFNV allow=2\$" "$OUT/fixture_mlp.stdout" || fail fixture_mlp records line
grep -q '^SHADOW image_fnv=9232652764948363499 kind=2 nparams=705 data_bound=16622604375676129865 bound_matches=no$' "$OUT/fixture_mlp.stdout" || fail fixture_mlp image line
[ "$(count '^ADVICE seq=[0-9]+ arm=[0-7] incumbent=[0-7] oracle=[0-7] score=[0-9]+$' fixture_mlp)" -eq "$FIXCOUNT" ] || fail fixture_mlp advice count
[ "$(count '^COST records=780 scored=780 fallback=0 adv_cost=[0-9]+ oracle_cost=[0-9]+ incumbent_cost=[0-9]+$' fixture_mlp)" -eq 1 ] || fail fixture_mlp cost line
note "COST_WALL records=$FIXCOUNT ms=$(cat "$OUT/fixture_mlp.wall_ms") image=mlp fixture"
run fixture_affine 1 2048 1816 "$AFF" "$BUILD/fixture.rb" "$BUILD/allow.txt"; expect fixture_affine 0
shape fixture_affine || fail fixture_affine shape
grep -q '^SHADOW image_fnv=3416091170583141300 kind=1 nparams=96 data_bound=16622604375676129865 bound_matches=no$' "$OUT/fixture_affine.stdout" || fail fixture_affine image line
[ "$(count '^ADVICE ' fixture_affine)" -eq "$FIXCOUNT" ] || fail fixture_affine advice count
[ "$(count '^COST records=780 scored=780 fallback=0 ' fixture_affine)" -eq 1 ] || fail fixture_affine cost line
note "COST_WALL records=$FIXCOUNT ms=$(cat "$OUT/fixture_affine.wall_ms") image=affine fixture"

# 6. Kill switch: enable 0 prints only SHADOW disabled and exits 0 (the stream after the
# config is never read).
run disabled 0 2048 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow.txt"; expect disabled 0
[ "$(cat "$OUT/disabled.stdout")" = "SHADOW disabled" ] || fail kill switch output

# 7. Process-path refusals. Every mutated record file is authorized through its own hash so
# the refusal under test is the record admission, not the allow-list.
cp "$MLP" "$BUILD/damaged.img"; printf '\001' | dd of="$BUILD/damaged.img" bs=1 seek=5000 conv=notrunc 2> /dev/null
run damaged_image 1 2048 11560 "$BUILD/damaged.img" "$BUILD/fixture.rb" "$BUILD/allow.txt"; expect damaged_image 0
shape damaged_image || fail damaged_image shape
grep -q '^FALLBACK all reason=image-5$' "$OUT/damaged_image.stdout" || fail damaged image not refused
[ "$(count '^ADVICE ' damaged_image)" -eq 0 ] || fail damaged image advised
head -c 11000 "$MLP" > "$BUILD/short.img"
run short_image 1 2048 11000 "$BUILD/short.img" "$BUILD/fixture.rb" "$BUILD/allow.txt"; expect short_image 0
grep -q '^FALLBACK all reason=image-1$' "$OUT/short_image.stdout" || fail truncated image not refused
cp "$BUILD/fixture.rb" "$BUILD/forged.rb"; printf '\001' | dd of="$BUILD/forged.rb" bs=1 seek=$((3 * 256 + 31 * 8)) conv=notrunc 2> /dev/null
printf '%s SYNTHETIC forged-fixture-test-only\n' "$(hash "$BUILD/forged.rb")" > "$BUILD/allow-forged.txt"
run forged_record 1 2048 11560 "$MLP" "$BUILD/forged.rb" "$BUILD/allow-forged.txt"; expect forged_record 66
grep -q '^FALLBACK all reason=records-7$' "$OUT/forged_record.stdout" || fail forged record not refused
cp "$BUILD/fixture.rb" "$BUILD/profile.rb"; { le64 1048576; } | dd of="$BUILD/profile.rb" bs=1 seek=$((0 * 256 + 10 * 8)) conv=notrunc 2> /dev/null
printf '%s SYNTHETIC profile-fixture-test-only\n' "$(hash "$BUILD/profile.rb")" > "$BUILD/allow-profile.txt"
run wrong_profile 1 2048 11560 "$MLP" "$BUILD/profile.rb" "$BUILD/allow-profile.txt"; expect wrong_profile 66
grep -q '^FALLBACK all reason=records-9$' "$OUT/wrong_profile.stdout" || fail wrong-profile record not refused
sed 's/^\(626048380840256264\) SYNTHETIC/\1 REVOKED/' "$BUILD/allow.txt" > "$BUILD/allow-revoked.txt"
grep -q "^$FIXFNV REVOKED" "$BUILD/allow-revoked.txt" || fail revoked allow-list edit
run revoked 1 2048 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow-revoked.txt"; expect revoked 67
grep -q '^FALLBACK all reason=allow-12$' "$OUT/revoked.stdout" || fail revoked dataset admitted
grep -v "^$FIXFNV " "$BUILD/allow.txt" > "$BUILD/allow-absent.txt"
run absent 1 2048 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow-absent.txt"; expect absent 67
grep -q '^FALLBACK all reason=allow-13$' "$OUT/absent.stdout" || fail unlisted dataset admitted
for f in damaged_image short_image forged_record wrong_profile revoked absent; do shape "$f" || fail "$f shape"; done
run budget10 1 10 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow.txt"; expect budget10 0
shape budget10 || fail budget10 shape
[ "$(count '^FALLBACK seq=[0-9]+ reason=budget$' budget10)" -eq $((FIXCOUNT - 10)) ] || fail budget fallback count
[ "$(count '^ADVICE ' budget10)" -eq 10 ] || fail budget advice count
grep -q '^COST records=780 scored=10 fallback=770 ' "$OUT/budget10.stdout" || fail budget cost line
# v2 admission (ORIG1-CONTRACT-v2.md): the retrieval advisor over the fixture scores the 468
# admitted records and falls back on the 312 refused ones; the v1 image under the v2 scorer
# is refused (image-9: its SS[20] is 0).
run fixture_v2_near 1 2048 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow.txt" 1 1; expect fixture_v2_near 0
shape fixture_v2_near || fail fixture_v2_near shape
grep -q '^SHADOW admission=v2 admitted=468 advisor=1$' "$OUT/fixture_v2_near.stdout" || fail fixture_v2_near admission line
[ "$(count '^FALLBACK seq=[0-9]+ reason=refused$' fixture_v2_near)" -eq 312 ] || fail fixture_v2_near refused count
grep -q '^COST records=780 scored=468 fallback=312 ' "$OUT/fixture_v2_near.stdout" || fail fixture_v2_near cost line
run fixture_v2_v1image 1 2048 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow.txt" 1 0; expect fixture_v2_v1image 0
grep -q '^FALLBACK all reason=image-9$' "$OUT/fixture_v2_v1image.stdout" || fail v1 image accepted under v2
[ "$(count '^ADVICE ' fixture_v2_v1image)" -eq 0 ] || fail v1 image advised under v2
run bad_config 1 4096 11560 "$MLP" "$BUILD/fixture.rb" "$BUILD/allow.txt"; expect bad_config 68
[ ! -s "$OUT/bad_config.stdout" ] || fail bad config printed
printf 'X' | "$SH" > "$OUT/bad_mode.stdout" 2> "$OUT/bad_mode.stderr"; echo $? > "$OUT/bad_mode.rc"; expect bad_mode 64
for f in fixture_mlp fixture_affine disabled damaged_image short_image forged_record wrong_profile revoked absent budget10 bad_config bad_mode; do
  [ -s "$OUT/$f.stderr" ] && fail "$f wrote to stderr"
done
note "refusals: damaged image-5, truncated image-1, forged records-7, wrong-profile records-9, revoked allow-12, absent allow-13, budget 10/770, config 68, mode 64"

# 8. Default-off proof, static: the tap's environment variable has exactly one reader in the
# code (the test harness), and the serving/runtime sources never reference the advisor.
readers=$(cd "$ROOT" && grep -rl 'OMEGA_EMPIRICAL_RECORDS' src tests/runtime tests/gates mk tools Makefile 2> /dev/null | sort)
echo "$readers" > "$OUT/tap-readers.txt"
[ "$readers" = "tests/runtime/rx_empirical_optimizer.c" ] || fail "OMEGA_EMPIRICAL_RECORDS readers: $readers"
srcrefs=$(cd "$ROOT" && grep -rlE 'omexp|orig4|shadow\.om|hvscore|shadow-advice' src | sort)
echo "$srcrefs" > "$OUT/src-advisor-refs.txt"
[ -z "$srcrefs" ] || fail "src/ references the advisor: $srcrefs"
note "default_off: tap readers = tests/runtime/rx_empirical_optimizer.c only; src/ references to omexp|orig4|shadow.om|hvscore|shadow-advice = none"

(cd "$OUT" && sha256sum -- *.stdout *.rc *.compile.stderr *.wall_ms candidates-check.txt tap-readers.txt src-advisor-refs.txt) > "$OUT/raw.sha256"
(cd "$BUILD" && sha256sum -- *.elf *.om *.rb *.img *.txt) > "$OUT/build.sha256"
note "ORIG4_SHADOW_RUN_PASS"
