#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# OM-2 qualification L836 (#748), phaseB/sha-f5d14cb2: independent qualification of the OM-2 SHA unit
# (aien-dev/om2/sha-compact @ f5d14cb2, om/sha256/sha256-om2.om) compiled by the S4 omc and bound by
# omega-bridge feat/slice-abi @ 3bb8b8a9. Tasks 1, 2, 4, 5 of candidates/sha/RESULTS.txt; task 3 is
# candidates/sha/mutants/run-mutants.sh. Writes one log per item into EVID; scratch in S.
# Usage (repository root): sh seed/qualification/om2/candidates/sha/run-qual.sh [EVID [S]]
set -u
ROOT=$(pwd)
Q=$ROOT/seed/qualification/om2
OR=$Q/oracle/sha
EVID=${1:-$Q/evidence/phaseB/sha-f5d14cb2}
S=${2:-$ROOT/build/om2-qual/sha/run}
UNIT=/tmp/zl/qsha/om/sha256/sha256-om2.om
OMC2=/tmp/zl/omc-f6a6492f
PIN2=499236398aabc652e2dae27fe4bdc4c5db3d3726314356e0dbef23e3345803ec
OMC1=$ROOT/seed/out/omc
PIN1=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
BRIDGE=/tmp/zl/qbridge
SUM=/usr/bin/sha256sum
mkdir -p "$EVID"; rm -rf "$S"; mkdir -p "$S"
h() { $SUM < "$1" | cut -d' ' -f1; }
run() { # NAME CMD...: log stdout+stderr and the exit status
  n=$1; shift
  { echo "\$ $*"; "$@"; echo "EXIT $?"; } > "$EVID/$n.txt" 2>&1
  tail -n 3 "$EVID/$n.txt" | sed "s/^/[$n] /"
}

# ---- identities -----------------------------------------------------------------------------------
{
  echo "date $(date -u +%FT%TZ) host $(uname -srm)"
  echo "unit $UNIT sha256 $(h "$UNIT") bytes $(wc -c < "$UNIT") lines $(wc -l < "$UNIT")"
  echo "unit repo HEAD $(git -C /tmp/zl/qsha rev-parse HEAD) status-clean=$([ -z "$(git -C /tmp/zl/qsha status --porcelain)" ] && echo yes || echo no)"
  echo "unit blob at HEAD sha256 $(git -C /tmp/zl/qsha show HEAD:om/sha256/sha256-om2.om | $SUM | cut -d' ' -f1)"
  echo "omc S4 $OMC2 sha256 $(h "$OMC2") (expected $PIN2)"
  echo "omc OM-1 pinned $OMC1 sha256 $(h "$OMC1") (expected $PIN1)"
  echo "bridge HEAD $(git -C $BRIDGE rev-parse HEAD) status-clean=$([ -z "$(git -C $BRIDGE status --porcelain)" ] && echo yes || echo no)"
  echo "bridge src/native.om sha256 $(h $BRIDGE/src/native.om); tools/native sha256 $(h $BRIDGE/tools/native)"
  $SUM --version | head -1; /usr/bin/python3 --version; gcc --version | head -1; objdump --version | head -1
} > "$EVID/identity.txt" 2>&1
[ "$(h "$OMC2")" = $PIN2 ] && [ "$(h "$OMC1")" = $PIN1 ] || { echo "compiler identity mismatch"; exit 3; }

# ---- task 1: compile ------------------------------------------------------------------------------
"$OMC2" < "$UNIT" > "$S/sha.bin"; e1=$?
"$OMC2" < "$UNIT" > "$S/sha2.bin"; e2=$?
{
  echo "\$ $OMC2 < $UNIT > sha.bin   (twice)"
  echo "exit $e1 $e2; deterministic=$(cmp -s "$S/sha.bin" "$S/sha2.bin" && echo yes || echo no)"
  echo "artifact sha256 $(h "$S/sha.bin") bytes $(wc -c < "$S/sha.bin") words $(( $(wc -c < "$S/sha.bin") / 4 ))"
  objdump -D -b binary -m aarch64 "$S/sha.bin" | sed -n '8,11p'
} > "$EVID/build.txt"
cp "$S/sha.bin" "$EVID/sha256-om2.S4.bin"

# ---- task 2: binding ------------------------------------------------------------------------------
B=$S/bind
OMEGA_NATIVE_OMC=$OMC1 OMEGA_NATIVE_OMC2=$OMC2 sh $BRIDGE/tools/native "$B" "$UNIT" > "$S/native.log" 2>&1; eb=$?
{
  echo "\$ OMEGA_NATIVE_OMC=$OMC1 OMEGA_NATIVE_OMC2=$OMC2 sh $BRIDGE/tools/native bind $UNIT"
  cat "$S/native.log"; echo "exit $eb"
  echo "generator native-gen (src/native.om compiled by the pinned OM-1 omc) sha256 $(h "$B/work/native-gen")"
  for f in omega_native.c omega_native.h omega_api.h omega_native.py omega_native.rs omega_native.zig omega_native.go libomega_native.so work/ident.txt work/sha256-om2.bin; do
    echo "sha256 $(h "$B/$f") $f"
  done
  echo "work/sha256-om2.bin == S4 artifact: $(cmp -s "$B/work/sha256-om2.bin" "$S/sha.bin" && echo yes || echo no)"
  s=$(objdump -t "$B/libomega_native.so" | awk '$NF=="ombr2_code_sha256_om2"{print $1}')
  off=$(objdump -h "$B/libomega_native.so" | awk '$2 ~ /^\./ {print $3, $4, $6}' | while read -r sz vma fo; do
    if [ $((0x$vma)) -le $((0x$s)) ] && [ $((0x$s)) -lt $((0x$vma + 0x$sz)) ]; then echo $((0x$s - 0x$vma + 0x$fo)); break; fi; done)
  echo "embedded code (ombr2_code_sha256_om2, file offset $off) == compiler bytes: $(dd if="$B/libomega_native.so" bs=1 skip="$off" count="$(wc -c < "$S/sha.bin")" 2> /dev/null | cmp -s - "$S/sha.bin" && echo yes || echo no)"
  echo "--- defined dynamic symbols"
  nm -D --defined-only "$B/libomega_native.so" | awk '{print $3}'
  echo "--- v1 symbols (omega_*): $(nm -D "$B/libomega_native.so" | awk '{print $NF}' | grep -c '^omega_')"
  printf '#include <stdint.h>\nuint32_t omega_sha256_init(uint64_t *r, uint8_t *c);\nint main(void){ uint64_t r; uint8_t c[104]; return (int)omega_sha256_init(&r, c); }\n' > "$S/v1use.c"
  echo "--- v1 caller link (must fail):"
  gcc "$S/v1use.c" -L"$B" -lomega_native -o "$S/v1use" 2>&1; echo "link exit $?"
  echo "--- omega_api.h"; cat "$B/omega_api.h"
  echo "--- omega_native.h"; cat "$B/omega_native.h"
} > "$EVID/binding.txt" 2>&1
CF="-std=gnu11 -O2 -Wall -Wextra -Werror"
gcc $CF -I"$B" "$OR/sha_qual.c" -L"$B" -lomega_native -Wl,-rpath,"$B" -o "$S/sha_qual" || exit 3
gcc $CF -DGENHDR='"omega_api.h"' -I"$B" -I"$Q/oracle/s5s7/sha" "$Q/oracle/s5s7/sha/sha_driver.c" -L"$B" -lomega_native -Wl,-rpath,"$B" -o "$S/sha_driver" || exit 3
gcc $CF -I"$B" "$OR/trace_child.c" -L"$B" -lomega_native -Wl,-rpath,"$B" -o "$S/trace_child" || exit 3
for c in 1 2; do gcc $CF -DCONTROL=$c -I"$B" "$OR/trace_child.c" -L"$B" -lomega_native -Wl,-rpath,"$B" -o "$S/trace_ctl$c" || exit 3; done

IDENT="omega-ident 1 unit=sha256_om2 abi=2 profile=OM-2 src=$(h "$UNIT") omc=$(h "$OMC2")"
run t1-vecoracle /usr/bin/python3 "$OR/vecoracle.py" "$Q/fixtures/sha256-vectors.txt"
run t1-vec "$S/sha_qual" vec "$Q/fixtures/sha256-vectors.txt" "$ROOT"
run t1-s5s7-sha "$S/sha_driver"
run t1-s5s7-sha-big nice -n 19 "$S/sha_driver" big
run t2-ident "$S/sha_qual" ident "$IDENT"
run t2-rcpt "$S/sha_qual" rcpt
run t2-rand748 nice -n 19 "$S/sha_qual" rand 748 1200
run t2-rand748-hashlib sh -c "/usr/bin/python3 '$OR/randcheck.py' < '$EVID/t2-rand748.txt'"
run t2-rand749 nice -n 19 "$S/sha_qual" rand 749 2000
run t2-rand749-hashlib sh -c "/usr/bin/python3 '$OR/randcheck.py' < '$EVID/t2-rand749.txt'"
run t2-part nice -n 19 "$S/sha_qual" part
run t2-bigzero nice -n 19 "$S/sha_qual" bigzero
run t2-big sh -c "free -g; nice -n 19 '$S/sha_qual' big"
run t2-bigzero-oracle sh -c "head -c 4294967299 /dev/zero | $SUM; grep EXP_BIG_ZERO '$Q/oracle/s5s7/sha/expected.h'"

# ---- task 4: leakage ------------------------------------------------------------------------------
"$OMC1" < "$Q/leak/leakscan.om" > "$S/leakscan"; chmod +x "$S/leakscan"
{
  echo "\$ $OMC1 < seed/qualification/om2/leak/leakscan.om > leakscan"
  echo "leakscan.om sha256 $(h "$Q/leak/leakscan.om"); leakscan binary sha256 $(h "$S/leakscan")"
  echo "build/om2-qual/leakscan-e13 sha256 $(h "$ROOT/build/om2-qual/leakscan-e13" 2> /dev/null)"
  for k in 0 1 2 3; do /usr/bin/python3 "$OR/entry.py" "$S/sha.bin" $k "$S/entry$k.bin"; echo "entry$k.bin sha256 $(h "$S/entry$k.bin")"; done
} > "$EVID/t4-leak-setup.txt" 2>&1
for c in "init entry0" "init-pub entry0 pub:0:32:8" "update entry1" "update-pub entry1 pub:0:32:8" \
         "final entry2" "final-pub entry2 pub:0:32:8" "hash entry3"; do
  set -- $c; n=$1; e=$2; shift 2
  { echo "\$ leakscan $* < $e.bin"; "$S/leakscan" "$@" < "$S/$e.bin"; echo "EXIT $?"; } > "$EVID/t4-leak-$n.txt" 2>&1
  { echo "\$ xcheck.py $e.bin t4-leak-$n.txt"; /usr/bin/python3 "$Q/oracle/leak/xcheck.py" "$S/$e.bin" "$EVID/t4-leak-$n.txt"; echo "EXIT $?"; } > "$EVID/t4-xcheck-$n.txt" 2>&1
  echo "[t4-leak-$n] $(grep -E '^(SUMMARY|VERDICT)' "$EVID/t4-leak-$n.txt" | tr '\n' ' ') revoked=$(grep -c PUBLIC-MEM-REVOKED "$EVID/t4-leak-$n.txt") xcheck=$(tail -n 2 "$EVID/t4-xcheck-$n.txt" | head -n 1)"
done
rm -rf "$S/leakgate"
run t4-leak-gate-pinned nice -n 19 "$ROOT/build/om2-qual/gate-p" "$OMC1" $PIN1 "$Q/leak/leak.cases" "$S/leakgate"
run t4-trace nice -n 19 /usr/bin/python3 "$OR/trace.py" "$S/trace_child" "$B/libomega_native.so" "$S/sha.bin"
run t4-trace-control1 nice -n 19 /usr/bin/python3 "$OR/trace.py" "$S/trace_ctl1" "$B/libomega_native.so" "$S/sha.bin"
run t4-trace-control2 nice -n 19 /usr/bin/python3 "$OR/trace.py" "$S/trace_ctl2" "$B/libomega_native.so" "$S/sha.bin"

# ---- task 5: size, rotates, stack -----------------------------------------------------------------
run t5-size-stack /usr/bin/python3 "$OR/stack.py" "$S/sha.bin"
