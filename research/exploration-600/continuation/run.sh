#!/bin/sh
# Test/research harness only. The decision executable is seed-built native Omega.
set -eu
cd "$(dirname "$0")/../../.."
: "${SEED_OMC:?Set SEED_OMC to the qualified seed-native omc (hash below)}"
: "${L523_OUT:?Set L523_OUT to a fresh absolute output directory}"
case "$L523_OUT" in /*) ;; *) echo 'output must be absolute' >&2; exit 2;; esac
if test -e "$L523_OUT"; then echo 'refusing to overwrite existing output' >&2; exit 2; fi
mkdir -p "$L523_OUT"
seedhash=$(sha256sum "$SEED_OMC" | cut -d' ' -f1)
test "$seedhash" = 01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6
quiet-build "$SEED_OMC" < research/exploration-600/continuation/guard.om > "$L523_OUT/guard"
test -s "$L523_OUT/guard"
python3 - "$L523_OUT/guard" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1]); b=p.read_bytes()
assert len(b)>64 and b[:6]==b'\x7fELF\x02\x01' and int.from_bytes(b[18:20],'little')==183
p.chmod(0o755)
PY
sha256sum "$SEED_OMC" "$L523_OUT/guard" > "$L523_OUT/binaries.sha256"
sha256sum research/exploration-600/continuation/* > "$L523_OUT/sources.sha256"
quiet-build timeout 60s python3 research/exploration-600/continuation/test_guard.py "$L523_OUT/guard" > "$L523_OUT/checks.json"
quiet-build timeout 120s python3 research/exploration-600/continuation/mutants.py "$L523_OUT/mutants" > "$L523_OUT/mutants-summary.json"
quiet-build timeout 120s python3 research/exploration-600/continuation/replay.py "$L523_OUT/guard" "$L523_OUT/replay" > "$L523_OUT/replay-summary.json"
EX600_OUT="$L523_OUT/regression-build" bash research/exploration-600/run.sh checks "$L523_OUT/regression" > "$L523_OUT/regression.log" 2>&1
printf 'NATIVE_GUARD_TESTS=PASS\nMUTANTS=PASS\nKNOWN_TRACE_REPLAY=COMPLETED_NOT_HELDOUT\nORIGINAL_REGRESSION=PASS\nORIGINAL_SCIENTIFIC_RESULT=FAIL_UNCHANGED\nGUARD_PROMOTION=REJECTED_NO_BENEFIT_PROVEN\n'
