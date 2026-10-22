#!/bin/sh
# Mutation copies only. No production edits or visibility policy changes.
set -eu
out=${1:?private output directory}; fs=${2:?private freestanding object directory}
mkdir -p "$out"
set --
for object in "$fs"/*.o; do
    case "$object" in */omega_gpu_session.o|*/omega_gpu_freestanding.o) continue;; esac
    set -- "$@" "$object"
done
killed=0
mutant() {
    name=$1; expression=$2; shift 2
    sed "$expression" src/omega_gpu_session.c > "$out/$name.c"
    if cmp -s src/omega_gpu_session.c "$out/$name.c"; then echo "unchanged mutant $name" >&2; exit 1; fi
    cc -std=gnu11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector \
        -U_FORTIFY_SOURCE -DOMEGA_GPU_NO_POSIX -DOMEGA_NUMERIC_CPU_ONLY -Isrc \
        -c -o "$out/$name.o" "$out/$name.c"
    cc -std=gnu11 -O2 -Wall -Wextra -Werror -DOMEGA_GPU_NO_POSIX -Isrc \
        -o "$out/$name" tests/gpu_session_visibility_oracle.c "$out/$name.o" "$@"
    if "$out/$name" > "$out/$name.log" 2>&1; then echo "SURVIVED $name" >&2; exit 1; fi
    # Require a failed assertion, not merely a crash or unrelated exit.
    if ! grep -q 'expect failed' "$out/$name.log"; then echo "no failed assertion $name" >&2; exit 1; fi
    killed=$((killed+1)); echo "killed: $name"
}
mutant omitted 's/    int rc = to_gpu ?.*;/    int rc = 0; (void)to_gpu;/' "$@"
mutant shortened 's/m->handle, 0, m->size)/m->handle, 0, m->size - 1)/g' "$@"
mutant reversed 's/sync_cpu_to_gpu/SYNC_TEMP/g;s/sync_gpu_to_cpu/sync_cpu_to_gpu/g;s/SYNC_TEMP/sync_gpu_to_cpu/g' "$@"
mutant swallowed 's/if (rc != OMEGA_GPU_PLAT_OK) { fail_plat(to_gpu/if (0 \&\& rc != OMEGA_GPU_PLAT_OK) { fail_plat(to_gpu/' "$@"
test "$killed" = 4
echo "session visibility mutants: $killed/4 compiled, failed assertions confirmed"
