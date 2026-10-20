#!/usr/bin/env bash
# tools/omega_emit/structs/gate_structs.sh <omega_emit> <oscc> <oscir> <checker2> <evidence dir>  (L093)
# Gate RT: for every positive .osc in tests/compiler that declares a struct, and the new programs in
# this folder: oscc IR bytes == omega_emit emit (omega_emit lift (those bytes)), compared by sha256.
# A is the ir_sha256 oscc prints. The bytes come from oscir (same front end, IR only), because oscc's
# unit container refuses struct-reference parameters; a file whose oscir bytes do not hash to A FAILs.
# Gate C2: checker2 gives the same first line (verdict and receipt digest) for both byte strings,
# and that line is an ACCEPT.
# Writes <evidence dir>/round-trip.md (every file, both digests, both checker2 lines). Exit 0 only if
# every file passes both gates; a file lift refuses is a FAIL with lift's reason in the table.
set -euo pipefail
E="${1:?usage: gate_structs.sh <omega_emit> <oscc> <oscir> <checker2> <evidence dir>}"
OSCC="${2:?oscc}"; OSCIR="${3:?oscir}"; C2="${4:?checker2}"; OUT="${5:?evidence dir}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
cd "$ROOT"
mkdir -p "$OUT"
T="$(mktemp -d "${TMPDIR:-/tmp}/omega-emit-structs.XXXXXXXX")"
trap 'rm -rf "$T"' EXIT
FILES="$( { find tests/compiler -name '*.osc' -not -path '*/neg/*' | xargs grep -l '^ *struct ' ; ls tools/omega_emit/structs/*.osc; } | sort -u)"
MD="$OUT/round-trip.md"
n=0; n_rt=0; n_c2=0
{
  echo "# EMIT-STRUCTS round trip (L093)"
  echo
  echo "Commit: $(git rev-parse HEAD)$(git diff --quiet HEAD -- tools/omega_emit src || echo ' (+ uncommitted changes)')"
  echo "Date: $(date -u +%FT%TZ)"
  echo
  echo "A = sha256 of the IR bytes oscc writes. B = sha256 of omega_emit emit(omega_emit lift(those bytes))."
  echo "RT passes when A == B. C2 passes when checker2 check gives the same ACCEPT line for both."
  echo
  echo "| file | IR version | A (oscc) | B (emit of lift) | RT | checker2 (oscc bytes) | checker2 (emitted bytes) | C2 |"
  echo "|---|---|---|---|---|---|---|---|"
} > "$MD"
for f in $FILES; do
  n=$((n + 1)); b="$(echo "$f" | tr / _)"
  if ! "$OSCC" "$f" > "$T/$b.oscc.out" 2>&1; then
    echo "| \`$f\` | - | oscc refused: $(head -1 "$T/$b.oscc.out" | tr '|' '/') | - | FAIL | - | - | FAIL |" >> "$MD"; continue
  fi
  A="$(sed -n 's/^ir_sha256=//p' "$T/$b.oscc.out")"
  if ! "$OSCIR" "$f" "$T/$b.ir" > "$T/$b.oscir.out" 2>&1 || [ "$(sed -n 's/^ir_sha256=//p' "$T/$b.oscir.out")" != "$A" ] ||
     [ "$(sha256sum < "$T/$b.ir" | cut -c1-64)" != "$A" ]; then
    echo "| \`$f\` | - | \`$A\` | oscir bytes do not hash to oscc's digest: $(head -1 "$T/$b.oscir.out" | tr '|' '/') | FAIL | - | - | FAIL |" >> "$MD"; continue
  fi
  V="$(od -An -tu1 -j7 -N1 "$T/$b.ir" | tr -d ' ')"
  c2a="$("$C2" check "$T/$b.ir" 2>&1 | head -1 || true)"
  if ! "$E" lift "$T/$b.ir" > "$T/$b.json" 2> "$T/$b.lift.err"; then
    echo "| \`$f\` | $V | \`$A\` | lift: $(head -1 "$T/$b.json" | tr '|' '/') | FAIL | \`$c2a\` | - | FAIL |" >> "$MD"; continue
  fi
  if ! "$E" emit "$T/$b.json" "$T/$b.re" > "$T/$b.emit.out"; then
    echo "| \`$f\` | $V | \`$A\` | emit: $(head -1 "$T/$b.emit.out" | tr '|' '/') | FAIL | \`$c2a\` | - | FAIL |" >> "$MD"; continue
  fi
  B="$(sha256sum < "$T/$b.re" | cut -c1-64)"
  c2b="$("$C2" check "$T/$b.re" 2>&1 | head -1 || true)"
  rt=FAIL; [ "$A" = "$B" ] && rt=PASS && n_rt=$((n_rt + 1))
  c2=FAIL; case "$c2a" in checker2=ACCEPT*) [ "$c2a" = "$c2b" ] && c2=PASS && n_c2=$((n_c2 + 1)) ;; esac
  echo "| \`$f\` | $V | \`$A\` | \`$B\` | $rt | \`$c2a\` | \`$c2b\` | $c2 |" >> "$MD"
  cp "$T/$b.json" "$OUT/$(basename "$f" .osc).oplist.json"
done
{
  echo
  echo "Gate RT: $n_rt/$n byte-identical. Gate C2: $n_c2/$n checker2 ACCEPT with the same line on both paths."
} >> "$MD"
echo "gate RT: $n_rt/$n; gate C2: $n_c2/$n (table: $MD)"
[ "$n_rt" = "$n" ] && [ "$n_c2" = "$n" ] && [ "$n" -ge 11 ]
