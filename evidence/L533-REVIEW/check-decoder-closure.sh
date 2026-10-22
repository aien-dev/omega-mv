#!/bin/sh
set -eu
root=${1:?exact PR499 export}
for file in src/osh/osh_expand.osc src/osh/osh_lex.osc src/osh/osh_parse.osc tests/physics0/osc/pd0_helpers.osc; do
 if ! test -f "$root/$file"; then echo "FAIL missing declared decoder corpus: $file"; exit 1; fi
done
echo 'PASS all four previously omitted corpus inputs exist'
