#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# E1 SPIR-V tier: build one SPIR-V module per op from seeded .om (IR mode) with
# the compiler in $OMC, then validate it with spirv-val (opencl2.1).
#   seed/spirv/e1/build.sh OUTDIR [OP...]     OP in exp2 log2 sigmoid tanh rsqrt erf gelu trig exp log
# Module OP = softf.om consts.om core.om [erfcxt.om for erf, gelu] OP.om.
# Kernel names: e1_<op> (trig.om holds e1_sin and e1_cos).
# Env: OMC (default build/seed/ir/stage2), IRSPIRV (default build/seed/spirv/irspirv),
#      SPIRV_VAL (default spirv-val), E1_MUTATE=CONST flips mantissa bit 12
#      of that consts.om constant (negative control).
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: build.sh OUTDIR [OP...]}
shift
[ $# -gt 0 ] || set -- exp2 log2 sigmoid tanh rsqrt erf gelu trig exp log
OMC=${OMC:-build/seed/ir/stage2}
IRSPIRV=${IRSPIRV:-build/seed/spirv/irspirv}
SPIRV_VAL=${SPIRV_VAL:-spirv-val}
mkdir -p "$out"
consts="$here/consts.om"
if [ -n "${E1_MUTATE:-}" ]; then
  name=$E1_MUTATE
  v=$(awk -v n="$name" '$1 == "const" && $2 == n { print $4 }' "$consts")
  [ -n "$v" ] || { echo "E1_MUTATE: no const $name" >&2; exit 2; }
  awk -v n="$name" -v m="$((v ^ 4096))" '$1 == "const" && $2 == n { $4 = m } { print }' \
    "$consts" > "$out/consts.mut.om"
  consts="$out/consts.mut.om"
fi
for op in "$@"; do
  [ -f "$here/$op.om" ] || { echo "unknown op $op" >&2; exit 2; }
  extra=
  case $op in erf|gelu) extra="$here/erfcxt.om" ;; esac
  { printf 'ir\n'; cat "$here/softf.om" "$consts" "$here/core.om" $extra "$here/$op.om"; } > "$out/$op.om"
  "$OMC" < "$out/$op.om" > "$out/$op.ir" || { echo "omc failed on $op (rc $?)" >&2; exit 1; }
  "$IRSPIRV" < "$out/$op.ir" > "$out/$op.spv" || { echo "irspirv failed on $op" >&2; exit 1; }
  "$SPIRV_VAL" --target-env opencl2.1 "$out/$op.spv"
  echo "built $op: $(sha256sum < "$out/$op.spv" | cut -c1-16)"
done
