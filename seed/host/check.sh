#!/bin/sh
# Entire host-extension qualification uses the seed chain and Omega programs.
set -eu
cd "$(dirname "$0")/../.."
sh seed/omega-from-seed.sh
sh seed/omc/gate7.sh
sh seed/host/gate.sh
sh seed/host/negative.sh
sh seed/host/regression.sh
if readelf -l -d seed/out/omc | grep -Eq 'INTERP|NEEDED'; then
    echo 'FAIL unexpected compiler runtime dependency' >&2
    exit 1
fi
printf 'PASS native-host qualification; no C/Rust compiler, assembler or linker used\n'
