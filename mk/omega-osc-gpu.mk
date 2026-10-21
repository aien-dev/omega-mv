# Omegese GPU back end (L386, C1 of docs/OMEGA.md): src/omega_osc_gpu.c builds sm_121 code for an
# Omegese kernel through the existing BlackwellIR builder. Picked up by `-include mk/*.mk`; not part
# of `all` or `test`.
#   make test-osc-gpu    host only: every accepted kernel of tests/checker2/kernels and tests/osc_gpu/kernels
#                        through omega_emit, checker2 and the back end, then on the SIMT warp simulator
#                        against host references (bit-exact), plus the back end's mutants (each must fail)
#   make test-osc-gpu-chip   the same kernel tests on the GB10 through omega_kernel_launch (src/omega_osc_launch.c):
#                        bit-exact except exp/inv/root (MUFU), which pass within 2^-20 relative (GPU.md s5).
#                        Run it inside a chip window (spark-window run); OSC_GPU_MUTANT=n is the red run.
ifndef OMEGA_OSC_GPU_MK
OMEGA_OSC_GPU_MK := 1
.PHONY: test-osc-gpu test-osc-gpu-chip
OSG_DIR = $(OUT_DIR)/osc_gpu
OSG_CFLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -ffp-contract=off -Isrc -Isrc/compiler -Itests
OSG_OSC_LIB = $(filter-out src/compiler/oscc_main.c,$(wildcard src/compiler/*.c)) src/sha256.c
OSG_SRCS = src/omega_osc_gpu.c src/omega_blackwell_codegen.c src/omega_blackwell_matmul.c src/omega_osc_launch_args.c
OSG_HDRS = src/omega_osc_gpu.h src/omega_osc_launch.h src/omega_gpu_session.h src/omega_blackwell_codegen.h src/omega_bw_em.h tests/bw_warp_sim.h $(wildcard src/compiler/*.h)

$(OSG_DIR)/test_osc_gpu: tests/osc_gpu/test_osc_gpu.c $(OSG_SRCS) $(OSG_OSC_LIB) $(OSG_HDRS)
	@mkdir -p $(OSG_DIR)
	$(CC) $(OSG_CFLAGS) -o $@ tests/osc_gpu/test_osc_gpu.c $(OSG_SRCS) $(OSG_OSC_LIB) -lm

OSG_CHIP_SRCS = src/omega_osc_launch.c src/omega_gpu_session.c src/omega_gpu_platform.c src/omega_gpu_platform_linux.c src/omega_gpu_fmt.c \
	src/omega_blackwell_qmd.c $(PHYSICS_DIR)/m16/m16_native.c $(PHYSICS_DIR)/nvrm/nvrm.c
$(OSG_DIR)/test_osc_gpu_chip: tests/osc_gpu/test_osc_gpu.c $(OSG_SRCS) $(OSG_CHIP_SRCS) $(OSG_OSC_LIB) $(OSG_HDRS)
	@mkdir -p $(OSG_DIR)
	$(CC) $(OSG_CFLAGS) -DOSC_CHIP -Wno-unused-function -pthread $(GPU_ENGINE_PHYS_INC) -o $@ tests/osc_gpu/test_osc_gpu.c \
		$(OSG_SRCS) $(OSG_CHIP_SRCS) $(OSG_OSC_LIB) -lm

test-osc-gpu: $(OSG_DIR)/test_osc_gpu $(OUT_DIR)/omega_emit/omega_emit $(OUT_DIR)/checker2/checker2
	sh tests/osc_gpu/run.sh $(OUT_DIR)/omega_emit/omega_emit $(OUT_DIR)/checker2/checker2 $(OSG_DIR)/test_osc_gpu $(OSG_DIR)/ir

test-osc-gpu-chip: $(OSG_DIR)/test_osc_gpu_chip test-osc-gpu
	$(OSG_DIR)/test_osc_gpu_chip $(OSG_DIR)/ir
endif
