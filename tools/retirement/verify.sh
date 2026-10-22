#!/usr/bin/env bash
# Reproducible audit/test driver. Never on the installed native execution path.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
E=$ROOT/evidence/seed-retirement
mkdir -p "$E"
quiet-build bash tools/retirement/prepare.sh
quiet-build bash tools/retirement/seed-suite.sh
quiet-build bash tools/retirement/build.sh
python3 tools/retirement/trn1/check.py > "$E/green.log" 2>&1
python3 tools/retirement/trn1/transport.py > "$E/transport-green.log" 2>&1
quiet-build python3 tools/retirement/trn1/mutants.py > "$E/mutants.log" 2>&1
python3 tools/retirement/deploy.py > "$E/deployment.log" 2>&1
python3 tools/retirement/inventory.py > "$E/inventory.log"
python3 tools/retirement/policy.py > "$E/policy.json"
python3 tools/retirement/test_policy.py > "$E/policy-tests.log"
printf 'L496 CANDIDATE VERIFIED; production retirement remains PARTIAL\n'
