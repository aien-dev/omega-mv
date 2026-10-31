# M21 OMEGA_AUTODIFF (docs/autodiff/M21_OMEGA_AUTODIFF.md): reverse-mode tape
# over the M20 tensor CPU tier. Files under src/autodiff/ and
# tests/test_omega_autodiff.c. Picked up by `-include mk/*.mk`. Not part of
# `all` or `test`. CPU only: opens no device, never touches the GB10.
#
# test-autodiff            CPU tests plain and with ASan/UBSan
# test-autodiff-mutations  applies each MUT: source mutation of src/autodiff
#                          in a scratch copy and proves the test then fails
# autodiff-mutants         alias of test-autodiff-mutations
# test-autodiff-fixture    FIXTURE=<path>: replays a native .om tape fixture
#                          (native/autodiff664) on this C tape, CPU table;
#                          the --mutant-sign negative control must FAIL.
#                          The GB10 arm is tests/run_ad_chip.sh, not here.
#
# Own source lists (mk/tensor.mk is read after this file, so its variables
# are not set yet when these rules are parsed).
ifndef AUTODIFF_MK
AUTODIFF_MK := 1
.PHONY: test-autodiff test-autodiff-mutations autodiff-mutants test-autodiff-fixture
AUTODIFF_CFLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -ffp-contract=off -Isrc -Isrc/tensor \
	-Isrc/autodiff -DOMEGA_NUMERIC_CPU_ONLY
AUTODIFF_SRCS = src/autodiff/omega_autodiff.c
AUTODIFF_TENSOR_SRCS = src/tensor/omega_tensor.c src/tensor/omega_tensor_cpu.c \
	src/tensor/omega_tensor_reduce_seam.c src/omega_numeric_reduce.c src/omega_numeric_transc.c
AUTODIFF_DEPS = src/omega_numeric.c src/omega_numeric_provenance.c src/omega_blackwell_encoder.c \
	src/omega_blackwell_codegen.c src/omega_blackwell_matmul.c src/omega_blackwell_qmd.c src/omega_om_units.c
AUTODIFF_HDRS = src/autodiff/omega_autodiff.h src/tensor/omega_tensor.h src/tensor/omega_tensor_reduce_seam.h \
	src/omega_numeric_reduce.h src/omega_numeric.h src/sha256.h src/omega_numeric_transc.h

$(OUT_DIR)/test_omega_autodiff: tests/test_omega_autodiff.c $(AUTODIFF_SRCS) $(AUTODIFF_TENSOR_SRCS) \
		$(AUTODIFF_DEPS) $(AUTODIFF_HDRS)
	@mkdir -p $(OUT_DIR)
	gcc $(AUTODIFF_CFLAGS) -O2 -o $@ tests/test_omega_autodiff.c $(AUTODIFF_SRCS) $(AUTODIFF_TENSOR_SRCS) \
		$(AUTODIFF_DEPS)

$(OUT_DIR)/test_omega_autodiff_asan: tests/test_omega_autodiff.c $(AUTODIFF_SRCS) $(AUTODIFF_TENSOR_SRCS) \
		$(AUTODIFF_DEPS) $(AUTODIFF_HDRS)
	@mkdir -p $(OUT_DIR)
	gcc $(AUTODIFF_CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
		-o $@ tests/test_omega_autodiff.c $(AUTODIFF_SRCS) $(AUTODIFF_TENSOR_SRCS) $(AUTODIFF_DEPS)

test-autodiff: $(OUT_DIR)/test_omega_autodiff $(OUT_DIR)/test_omega_autodiff_asan
	./$(OUT_DIR)/test_omega_autodiff
	./$(OUT_DIR)/test_omega_autodiff_asan

$(OUT_DIR)/test_omega_ad_fixture: tests/test_omega_ad_fixture.c $(AUTODIFF_SRCS) $(AUTODIFF_TENSOR_SRCS) \
		$(AUTODIFF_DEPS) $(AUTODIFF_HDRS)
	@mkdir -p $(OUT_DIR)
	gcc $(AUTODIFF_CFLAGS) -O2 -o $@ tests/test_omega_ad_fixture.c $(AUTODIFF_SRCS) $(AUTODIFF_TENSOR_SRCS) \
		$(AUTODIFF_DEPS)

test-autodiff-fixture: $(OUT_DIR)/test_omega_ad_fixture
	@test -n "$(FIXTURE)" || { echo "test-autodiff-fixture needs FIXTURE=<path>"; exit 2; }
	./$(OUT_DIR)/test_omega_ad_fixture --host $(FIXTURE)
	@./$(OUT_DIR)/test_omega_ad_fixture --host --mutant-sign $(FIXTURE); rc=$$?; \
	 if [ $$rc -ne 1 ]; then echo "mutant-sign control did not FAIL (rc=$$rc)"; exit 1; fi; \
	 echo "AD_FIXTURE_HOST mutant-sign control: FAIL as required"

test-autodiff-mutations:
	sh tools/autodiff_mutations.sh

autodiff-mutants: test-autodiff-mutations
endif
