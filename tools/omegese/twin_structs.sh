#!/usr/bin/env bash
# tools/omegese/twin_structs.sh <omegese> <omega_emit> <oscc> <checker2> [<table.md>] -- the struct twin gate
# (docs/one_layer/OMEGESE.md section 3, "Structs"). Each of the six twins in tests/one_layer/omegese/structs
# is one program written twice, in OSC and in Omegese. Both paths must give the same IR bytes:
#   oscc t.osc ir_sha256 == sha256(omega_emit emit (omegese emit t.omegese)),
# where oscc runs without OUT: its ir_sha256 is the SHA-256 of the encoded IR bytes (osc_ir_digest), and
# the unit container that OUT writes refuses struct-reference parameters (twins 4 and 5),
# and checker2 must ACCEPT the unit Omegese made. With <table.md>, the 12 digests are written there as a
# Markdown table. Exit 0 only when all six are identical and accepted.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
[ $# -ge 4 ] && [ $# -le 5 ] || { echo "usage: twin_structs.sh <omegese> <omega_emit> <oscc> <checker2> [<table.md>]" >&2; exit 2; }
G="$1" OME="$2" OSCC="$3" C2="$4" OUT="${5:-}"
T="$(mktemp -d "${TMPDIR:-/tmp}/omegese-twins.XXXXXXXX")"; trap 'rm -rf "$T"' EXIT
fails=0 n=0
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
rows=""
for o in "$ROOT"/tests/one_layer/omegese/structs/t*.omegese; do
  id="$(basename "$o" .omegese)" s="${o%.omegese}.osc"
  covers="$(sed -n '2s/^\/\/ Omegese struct twin [0-9] (\(.*\)): .*/\1/p' "$s")"
  if ! "$OSCC" "$s" > "$T/$id.oscc" 2>&1; then bad "$id: oscc refused: $(head -2 "$T/$id.oscc")"; continue; fi
  hs="$(sed -n "s/^ir_sha256=//p" "$T/$id.oscc")"
  [ -n "$hs" ] || { bad "$id: oscc printed no ir_sha256"; continue; }
  if ! "$G" emit "$o" "$T/$id.json" > "$T/$id.g" 2>&1; then bad "$id: omegese: $(cat "$T/$id.g")"; continue; fi
  if ! "$OME" emit "$T/$id.json" "$T/$id.e.ir" > "$T/$id.e" 2>&1; then bad "$id: omega_emit: $(cat "$T/$id.e")"; continue; fi
  hg="$(sha256sum < "$T/$id.e.ir" | cut -d' ' -f1)"
  v="$("$C2" check "$T/$id.e.ir" 2>&1 | head -1 || true)"
  same=no; [ "$hs" = "$hg" ] && same=yes
  case "$v" in checker2=ACCEPT*) c2=ACCEPT ;; *) c2="$v" ;; esac
  [ "$same" = yes ] || bad "$id: IR differs: oscc $hs, omegese $hg"
  [ "$c2" = ACCEPT ] || bad "$id: checker2: $v"
  [ "$same" = yes ] && [ "$c2" = ACCEPT ] && n=$((n + 1))
  rows+="| $id | $covers | \`$hs\` | \`$hg\` | $same | $c2 |"$'\n'
done
if [ -n "$OUT" ]; then
  {
    echo "| twin | covers | sha256 of oscc IR (OSC) | sha256 of omega_emit IR (Omegese) | identical | checker2 |"
    echo "|---|---|---|---|---|---|"
    printf '%s' "$rows"
  } > "$OUT"
fi
echo "struct twins: $n/6 give byte-identical IR through oscc and through omegese -> omega_emit, checker2 ACCEPT"
[ "$n" -eq 6 ] && [ "$fails" -eq 0 ] || { echo "twin_structs: $fails failure(s)"; exit 1; }
echo "twin_structs: PASS"
