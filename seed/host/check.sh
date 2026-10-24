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
sh seed/host/gate.sh
sh seed/host/negative.sh
sh seed/host/regression.sh
if readelf -l -d seed/out/omc | grep -Eq 'INTERP|NEEDED'; then
    echo 'FAIL unexpected compiler runtime dependency' >&2
    exit 1
fi
printf 'PASS native-host qualification; no C/Rust compiler, assembler or linker used\n'
