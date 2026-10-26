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
grep -qx 'OM2GATE FAIL cases=1 pass=0 fail=1 invalid=0 not_run=0' "$run/mutant.log"
echo 'PASS contract-enforcement mutant detected by exact runtime-status mismatch'
sh seed/host/gate.sh
sh seed/host/negative.sh
sh seed/host/regression.sh
if readelf -l -d seed/out/omc | grep -Eq 'INTERP|NEEDED'; then
    echo 'FAIL unexpected compiler runtime dependency' >&2
    exit 1
fi
printf 'PASS native-host qualification; no C/Rust compiler, assembler or linker used\n'
