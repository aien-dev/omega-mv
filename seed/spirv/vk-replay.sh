#!/bin/sh
# Replays a gate-vkspirv.sh run on another Vulkan driver. DIR is a copy of the
# gate's build/seed/vkspirv directory (modules, inputs, native expected outputs
# and replay.tsv); HOST is vk-host built for the target driver (VKSPIRV_TARGET).
# Modules and expectations are produced on AArch64 Linux by seeded omc and the
# native programs; this script only executes and compares them, so a driver
# without omc (MoltenVK on macOS, a GPU box) is checked against the same bytes.
# replay.tsv: MODULE ENTRY NPARAMS INPUT EXPECTED match|mismatch (tab-separated,
# paths relative to DIR). A "mismatch" row is a negative control (opcode mutant)
# that must differ. Exit 0 all rows as required, 1 any failure, 2 usage.
set -eu
[ $# = 2 ] || { echo 'usage: vk-replay.sh HOST DIR' >&2; exit 2; }
HOST=$1; D=$2
[ -x "$HOST" ] && [ -f "$D/replay.tsv" ] || { echo 'vk-replay: HOST or DIR/replay.tsv missing' >&2; exit 2; }
tab=$(printf '\t'); rows=0; passed=0; controls=0; first=1
while IFS="$tab" read -r module entry np input expected want; do
    rows=$((rows+1))
    if "$HOST" "$D/$module" "$entry" "$np" < "$D/$input" > "$D/$input.replay" 2> "$D/$input.replay.err"; then :; else
        rc=$?; cat "$D/$input.replay.err" >&2
        echo "FAIL $module $entry host exited $rc"; exit 1
    fi
    if [ "$first" = 1 ]; then grep '^vk-host: \(target\|device\)' "$D/$input.replay.err"; first=0; fi
    if cmp -s "$D/$expected" "$D/$input.replay"; then got=match; else got=mismatch; fi
    [ "$got" = "$want" ] || { echo "FAIL $module $entry got=$got want=$want"; exit 1; }
    [ "$want" = match ] || controls=$((controls+1))
    passed=$((passed+1))
    echo "PASS $module $entry rows=$(wc -l < "$D/$input" | tr -d ' ') $got"
done < "$D/replay.tsv"
[ "$rows" -gt 0 ] && [ "$controls" -gt 0 ] || { echo 'FAIL empty replay or no negative control'; exit 1; }
echo "PASS VKREPLAY dispatches=$passed negative_controls=$controls"
