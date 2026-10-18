#!/usr/bin/env bash
# tools/test_gate_physics_pin.sh -- host self-test (omega#523, omega#508).
#
# Every tests/gates/run.sh step that builds against physics must take the physics commit
# from physics.lock, and every gate that runs such a step must list physics.lock as an input.
# Why: the Makefile's check-physics-lock refuses any physics checkout other than the lock, so
# a literal commit in run.sh turns every chip gate red on the next lock bump (it did for
# e95e3ed -> 6d7cf0d4 in #508), and a gate that does not list physics.lock is never selected
# by a lock-only bump, so the bump is judged without the gates it changes.
#
# Exit 0 when both hold, 1 with one line per violation otherwise. No GPU, no network.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
RUN="$ROOT/tests/gates/run.sh"
fail=0

# 1. No literal physics commit on a pin line.
if grep -nE '^[^#]*pin[[:space:]]+physics-src[^[:space:]]*[[:space:]]+physics[[:space:]]+[0-9a-f]{7,40}' "$RUN"; then
  echo "FAIL literal physics commit in tests/gates/run.sh (take it from physics.lock: physics_pin)"
  fail=1
fi

# 2. The run.sh case labels whose block calls physics_pin (or pins physics by hand).
labels="$(awk '
  /^  [^ #][^)]*\)/ && $0 !~ /^  *\(/ { lab=$0; sub(/^  /,"",lab); sub(/\).*/,"",lab) }
  /physics_pin|pin[ \t]+physics-src/ && lab != "" { print lab; lab="" }
' "$RUN")"
if [ -z "$labels" ]; then
  echo "FAIL found no run.sh step that pins physics (parser out of date?)"
  exit 1
fi

step_pins_physics() {
  local step="$1" lab pat
  while IFS= read -r lab; do
    IFS='|' read -r -a pats <<<"$lab"
    for pat in "${pats[@]}"; do
      # shellcheck disable=SC2254
      case "$step" in $pat) return 0 ;; esac
    done
  done <<<"$labels"
  return 1
}

checked=0
for g in "$ROOT"/tests/gates/*.gate; do
  step="$(sed -n 's/^[[:space:]]*args:[[:space:]]*\["\([^"]*\)".*/\1/p' "$g" | head -n 1)"
  [ -n "$step" ] || continue
  step_pins_physics "$step" || continue
  checked=$((checked + 1))
  if ! grep -qx '  - physics.lock' "$g"; then
    echo "FAIL $(basename "$g") runs step '$step', which builds against physics, but does not list physics.lock in inputs"
    fail=1
  fi
done

if [ "$checked" -eq 0 ]; then
  echo "FAIL no gate runs a physics-pinning step (parser out of date?)"
  exit 1
fi
if [ "$fail" -eq 0 ]; then
  echo "OMEGA_GATE_PHYSICS_PIN_PASS gates_checked=$checked"
fi
exit "$fail"
