#!/bin/sh
# L838 baseline: the historical SHA-256 measurement at omega main acb3f15c, reproduced with the
# historical harnesses om/sha256/parity/bench.c and small.c, unmodified. Both arms (Omega = src/sha256.c
# + src/omega_om_units.inc; C = src/sha256.c of a647b6a9, symbols renamed exactly as parity/run.sh does)
# are compiled with identical flags and run in one process, pinned to one Cortex-X925 core.
#   small.c: per length, 11 interleaved rounds of (C batch, Omega batch), 200,000 calls each.
#   bench.c: 64 MiB bulk (C then Omega) and 200,000 x 32-byte sha256_hash (Omega then C); run 11 times.
set -eu
REPO=${REPO:-/tmp/zl/main}
CPU=${CPU:-19}
FLAGS="-std=gnu11 -O2"
T=/tmp/zl/sha-base/build; rm -rf "$T"; mkdir -p "$T"
[ "$(git -C "$REPO" rev-parse HEAD)" = acb3f15cdc691b1b046b48b0dad0f9255a12ba4b ]
git -C "$REPO" status --porcelain -- src om | grep . && { echo "dirty baseline"; exit 1; } || true
git -C "$REPO" show a647b6a9821fffcf52ba9ce813f75afa4b84be62:src/sha256.c > "$T/c.c"
[ "$(/usr/bin/sha256sum < "$T/c.c" | cut -c1-64)" = a8fb88611e68ea5a876ee1658e1665994a03bfbed215983618d769f0e7205473 ]
sed 's/sha256_\(init\|update\|final\|hash\|transform\)/orc_sha256_\1/g; s/^static const uint32_t K\b/static const uint32_t ORC_K/; s/\bK\[/ORC_K[/g' "$T/c.c" > "$T/oracle.c"
cc $FLAGS -I"$REPO/src" -c "$REPO/src/sha256.c" -o "$T/new.o"
cc $FLAGS -I"$REPO/src" -c "$T/oracle.c" -o "$T/oracle.o"
cc $FLAGS -I"$REPO/src" "$REPO/om/sha256/parity/bench.c" "$T/new.o" "$T/oracle.o" -o "$T/bench" -pthread
cc $FLAGS -I"$REPO/src" "$REPO/om/sha256/parity/small.c" "$T/new.o" "$T/oracle.o" -o "$T/small" -pthread
echo "# baseline acb3f15c  $(date -u +%FT%TZ)  host $(uname -nrm)  cpu$CPU midr $(cat /sys/devices/system/cpu/cpu$CPU/regs/identification/midr_el1) max_khz $(cat /sys/devices/system/cpu/cpu$CPU/cpufreq/cpuinfo_max_freq)"
echo "# cc: $(cc --version | head -1); flags: $FLAGS (both arms)"
echo "# sha256: src/sha256.c $(/usr/bin/sha256sum < "$REPO/src/sha256.c" | cut -c1-16)  omega_om_units.inc $(/usr/bin/sha256sum < "$REPO/src/omega_om_units.inc" | cut -c1-16)  oracle a8fb8861  bench.c $(/usr/bin/sha256sum < "$REPO/om/sha256/parity/bench.c" | cut -c1-16)  small.c $(/usr/bin/sha256sum < "$REPO/om/sha256/parity/small.c" | cut -c1-16)"
echo "# loadavg before: $(cat /proc/loadavg)"
for r in 1 2 3 4 5 6 7 8 9 10 11; do echo "bench run $r"; taskset -c "$CPU" "$T/bench"; done
echo "small"
taskset -c "$CPU" "$T/small"
echo "# loadavg after: $(cat /proc/loadavg)"
