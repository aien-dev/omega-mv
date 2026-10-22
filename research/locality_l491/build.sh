#!/bin/sh
# Export immutable compiler inputs; never build in another owner's checkout.
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
case "$out" in /*) ;; *) out="$PWD/$out";; esac
seed=57f5f4258cebf3d5b2c22853cd062e351265c525
mkdir -p "$out/seed-src"
git archive "$seed" seed | tar -x -C "$out/seed-src"
(cd "$out/seed-src"; sh seed/omega-from-seed.sh) > "$out/seed.log" 2>&1
compiler="$out/seed-src/seed/out/omc"
test "$(sha256sum "$compiler" | cut -d' ' -f1)" = 01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6
"$compiler" < research/locality_l491/locality.om > "$out/locality"
chmod +x "$out/locality"
printf '%s\n' "$seed" > "$out/seed.commit"
sha256sum "$compiler" "$out/locality" > "$out/binaries.sha256"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2 \
 research/locality_l491/oracle.c -o "$out/oracle"
