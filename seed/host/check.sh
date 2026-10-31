#!/bin/sh
# Entire host-extension qualification uses the seed chain and Omega programs.
set -eu
cd "$(dirname "$0")/../.."
sh seed/omega-from-seed.sh
if [ -x build/omegese/omegese ] && [ -x build/omega_emit/omega_emit ] && [ -x build/checker2/checker2 ]; then
    sh seed/omc/gate-ir.sh
else
    printf 'SKIP gate-ir: C oracles not built\n'
    printf '  make build/omegese/omegese build/omega_emit/omega_emit build/checker2/checker2\n'
fi
sh seed/omc/gate7.sh
sh seed/omc/gate8.sh
sh seed/omc/gate9.sh
sh seed/omc/gate10.sh
sh seed/omc/gate11.sh
sh seed/omc/gate12.sh
sh seed/omc/gate13.sh
sh seed/omc/gate14.sh
sh seed/omc/gate15.sh
sh seed/omc/gate16.sh
sh seed/omc/gate17.sh
sh seed/omc/gate18.sh
sh seed/omc/gate-recursion.sh
# Executable contracts use the existing fail-closed Omega conductor.
B=build/seed/contracts
mkdir -p "$B"
seed/out/omc < seed/qualification/om2/gate.om > "$B/gate"
chmod +x "$B/gate"
pin=$(sha256sum seed/out/omc | cut -d' ' -f1)
run=$(mktemp -d "$B/run.XXXXXX")
"$B/gate" seed/out/omc "$pin" seed/omc/contracts/contracts.cases "$run/cases"
if "$B/gate" seed/out/omc "$pin" seed/omc/contracts/mutant.cases "$run/mutant" > "$run/mutant.log"; then
    echo 'FAIL contract-enforcement mutant survived' >&2
    exit 1
else
    status=$?
    [ "$status" = 1 ] || { cat "$run/mutant.log"; exit 1; }
fi
grep -qx 'VERDICT mutant-requires FAIL run exit=0 expected=109 stdout=match stderr=match' "$run/mutant.log"
grep -qx 'VERDICT mutant-proof FAIL run exit=0 expected=109 stdout=match stderr=match' "$run/mutant.log"
grep -qx 'OM2GATE FAIL cases=2 pass=0 fail=2 invalid=0 not_run=0' "$run/mutant.log"
echo 'PASS contract-enforcement mutant detected by exact runtime-status mismatch'
# Packed-list hostile ingress (#671): generated packets reach execprog from stdin.
"$B/gate" seed/out/omc "$pin" seed/qualification/security673/packets.cases "$run/packets"
if "$B/gate" seed/out/omc "$pin" seed/qualification/security673/packets-mutant.cases "$run/packets-mutant" > "$run/packets-mutant.log"; then
    echo 'FAIL packed-list bound mutant survived' >&2
    exit 1
else
    status=$?
    [ "$status" = 1 ] || { cat "$run/packets-mutant.log"; exit 1; }
fi
grep -qx 'VERDICT mutant-size FAIL run exit=0 expected=103 stdout=match stderr=match' "$run/packets-mutant.log"
grep -qx 'VERDICT mutant-count FAIL run exit=0 expected=103 stdout=match stderr=match' "$run/packets-mutant.log"
grep -qx 'VERDICT mutant-exact FAIL run exit=0 expected=103 stdout=match stderr=match' "$run/packets-mutant.log"
grep -qx 'OM2GATE FAIL cases=3 pass=0 fail=3 invalid=0 not_run=0' "$run/packets-mutant.log"
echo 'PASS packed-list ingress: hostile packets refused; size, count and exact-length mutants detected'
# OM-2 hex literals (#745): every place, decimal twins byte-identical, refusals on the decimal codes.
"$B/gate" seed/out/omc "$pin" seed/omc/om2/hex/hex.cases "$run/hex" > "$run/hex.log" || { cat "$run/hex.log"; exit 1; }
grep -qx 'OM2GATE PASS cases=51 pass=51 fail=0 invalid=0 not_run=0' "$run/hex.log"
if "$B/gate" seed/out/omc "$pin" seed/omc/om2/hex/mutants.cases "$run/hexmut" > "$run/hexmut.log"; then
    echo 'FAIL hex-literal mutant survived' >&2
    exit 1
else
    status=$?
    [ "$status" = 1 ] || { cat "$run/hexmut.log"; exit 1; }
fi
grep -qx 'OM2GATE FAIL cases=8 pass=3 fail=5 invalid=0 not_run=0' "$run/hexmut.log"
[ "$(grep -c '^VERDICT ctl-hex-[a-z]*-pairs-dec-run PASS ' "$run/hexmut.log")" = 3 ]
[ "$(grep -c '^VERDICT hex-[a-z]*-[a-z0-9-]* FAIL ' "$run/hexmut.log")" = 5 ]
echo 'PASS hex literals (51 cases) and 3 tok_hex mutants killed'
# OM-2 expression boundaries (#745, spec section 2): missing/extra operand and grouping, both profiles.
"$B/gate" seed/out/omc "$pin" seed/omc/om2/bnd/bnd.cases "$run/bnd" > "$run/bnd.log" || { cat "$run/bnd.log"; exit 1; }
grep -qx 'OM2GATE PASS cases=47 pass=47 fail=0 invalid=0 not_run=0' "$run/bnd.log"
if "$B/gate" seed/out/omc "$pin" seed/omc/om2/bnd/mutants.cases "$run/bndmut" > "$run/bndmut.log"; then
    echo 'FAIL operand-boundary mutant survived' >&2
    exit 1
else
    status=$?
    [ "$status" = 1 ] || { cat "$run/bndmut.log"; exit 1; }
fi
grep -qx 'OM2GATE FAIL cases=10 pass=2 fail=8 invalid=0 not_run=0' "$run/bndmut.log"
[ "$(grep -c '^VERDICT ctl-bnd-[a-z]*-g0-run PASS ' "$run/bndmut.log")" = 2 ]
[ "$(grep -c '^VERDICT bnd-[a-z]*-[a-z0-9-]* FAIL ' "$run/bndmut.log")" = 8 ]
echo 'PASS expression boundaries (47 cases) and 2 operand mutants killed'
# Same function-mode contract behavior through the unchanged C referee and OscRt.
if [ ! -x build/omegese/omegese ] || [ ! -x build/omega_emit/omega_emit ]; then
    echo 'NOT_RUN contract C parity: build the comparison oracles' >&2
    exit 3
fi
set --
for source in src/compiler/*.c; do
    [ "$source" = src/compiler/oscc_main.c ] || set -- "$@" "$source"
done
cc -O1 -Isrc -Isrc/compiler -o "$B/runner" seed/check/runner.c "$@" src/omega_om_units.c
build/omegese/omegese emit seed/omc/contracts/parity.omegese "$run/parity.json"
build/omega_emit/omega_emit emit "$run/parity.json" "$run/parity.oscir"
seed/out/omc < seed/omc/contracts/parity.om > "$run/parity.bin"
"$B/runner" "$run/parity.oscir" check seed/omc/contracts/parity.inputs > "$run/oracle.out"
"$B/runner" "$run/parity.oscir" check seed/omc/contracts/parity.inputs "$run/parity.bin" > "$run/native.out"
sed '1d' "$run/oracle.out" > "$run/oracle.rows"
sed '1d' "$run/native.out" > "$run/native.rows"
[ "$(wc -l < "$run/oracle.rows")" = "$(wc -l < seed/omc/contracts/parity.inputs)" ]
cmp "$run/oracle.rows" "$run/native.rows"
echo 'PASS contract C parity: exact values, canonical bits and traps'
sh seed/host/gate.sh
sh seed/host/negative.sh
sh seed/host/regression.sh
if readelf -l -d seed/out/omc | grep -Eq 'INTERP|NEEDED'; then
    echo 'FAIL unexpected compiler runtime dependency' >&2
    exit 1
fi
# OMEGA-CRYPTO-REUSE-1 (Refs #639). set -eu: a failing suite exits this script.
# A no-op tamper in om/tls/test/reuse/run.sh (replacement equal to the site) is not
# caught, so that suite, and therefore this script, exits nonzero.
sh om/tls/test/gcmsiv/run.sh seed/out/omc
sh om/tls/test/reuse/run.sh seed/out/omc
printf 'PASS native-host qualification; seeded build, C tools are comparison oracles only\n'
