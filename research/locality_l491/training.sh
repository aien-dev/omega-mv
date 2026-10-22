#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out=${L491_OUT:-research/locality_l491/out}
mkdir -p "$out"
flags='-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -ffp-contract=off -Isrc -Isrc/tensor -Isrc/autodiff -DOMEGA_NUMERIC_CPU_ONLY'
sources='src/autodiff/omega_autodiff.c src/tensor/omega_tensor.c src/tensor/omega_tensor_cpu.c src/tensor/omega_tensor_reduce_seam.c src/omega_numeric_reduce.c src/omega_numeric_transc.c src/omega_numeric.c src/omega_numeric_provenance.c src/omega_blackwell_encoder.c src/omega_blackwell_codegen.c src/omega_blackwell_matmul.c src/omega_blackwell_qmd.c src/sha256.c src/train/tg_store.c src/train/tg_sgd.c'
# These are reference-test builds of existing APIs, not a new C execution path.
cc $flags -O2 ${L491_TEST_CFLAGS:-} research/locality_l491/training_test.c $sources -lm -o "$out/training_test"
scratch=$(mktemp -d "$out/training-XXXXXXXX")
"$out/training_test" "$scratch"
