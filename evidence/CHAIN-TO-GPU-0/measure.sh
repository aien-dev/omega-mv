#!/usr/bin/env bash
# measure.sh -- bytes through each CPU-path stage for the 10 public exp445 dev tasks (L097, CHAIN_TO_GPU.md 5.1).
# Stages: Omegese text -> JSON op list (omegese emit) -> OSC binary IR (omega_emit emit).
# AArch64 code bytes come from oscc on the task's reference.osc: no CLI prints the code size of an IR file, so the
# code column is the TEXT spelling of the same task (its IR bytes are listed too; they may differ from the Omegese IR).
# Usage: measure.sh <omega worktree> (tools built with: make PHYSICS_DIR=/nonexistent PHYSICS_LOCK_CHECK=0 omega-emit omegese oscc)
set -euo pipefail
R="$(realpath -e "${1:?omega worktree}")"
G="$R/build/omegese/omegese" E="$R/build/omega_emit/omega_emit" C="$R/build/compiler/oscc"
for t in "$G" "$E" "$C"; do [ -x "$t" ] || { echo "missing $t" >&2; exit 2; }; done
W="$(mktemp -d "${TMPDIR:-/tmp}/chain-bytes.XXXXXXXX")"; trap 'rm -rf "$W"' EXIT
echo "omega $(git -C "$R" rev-parse HEAD)"
echo "| task | Omegese text | JSON op list | IR (from Omegese) | IR (from reference.osc) | AArch64 code (reference.osc) |"
echo "|---|---|---|---|---|---|"
tot=(0 0 0 0 0)
for n in 01 02 03 04 05 06 07 08 09 10; do
  src="$R/tests/one_layer/omegese/dev/dev$n.omegese" ref="$R/tests/one_layer/exp445/dev/tasks/cases/dev$n/hidden/reference.osc"
  "$G" emit "$src" "$W/$n.json" > /dev/null
  "$E" emit "$W/$n.json" "$W/$n.oscir" > /dev/null
  "$C" "$ref" "$W/ref$n" > /dev/null
  v=($(stat -c %s "$src" "$W/$n.json" "$W/$n.oscir" "$W/ref$n.ir" "$W/ref$n.code"))
  for i in 0 1 2 3 4; do tot[$i]=$((tot[$i] + v[$i])); done
  echo "| dev$n | ${v[0]} | ${v[1]} | ${v[2]} | ${v[3]} | ${v[4]} |"
done
echo "| **total** | **${tot[0]}** | **${tot[1]}** | **${tot[2]}** | **${tot[3]}** | **${tot[4]}** |"
