# #519 contained host contract. Existing graph recovery remains on PR588.
GPU_VIS_TEST := $(OUT_DIR)/gpu_session_visibility_oracle
$(GPU_VIS_TEST): tests/gpu_session_visibility_oracle.c tests/test_omega_gpu_session_platform.c mk/omega-gpu-engine.mk \
        $(wildcard src/omega_gpu_*.c src/omega_gpu_*.h src/omega_blackwell_*.c src/omega_blackwell_*.h) src/sha256.c
	$(MAKE) GPU_FS_DIR=$(OUT_DIR)/gpu-visibility-fs $(OUT_DIR)/gpu-visibility-fs/omega_gpu_freestanding.o
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Werror -Isrc -DOMEGA_GPU_NO_POSIX -o $@ tests/gpu_session_visibility_oracle.c $(OUT_DIR)/gpu-visibility-fs/omega_gpu_freestanding.o
.PHONY: test-gpu-session-visibility
test-gpu-session-visibility: $(GPU_VIS_TEST)
	$(GPU_VIS_TEST)
.PHONY: test-gpu-session-visibility-mutants
test-gpu-session-visibility-mutants: $(GPU_VIS_TEST)
	sh tests/gpu_session_visibility_mutants.sh $(OUT_DIR)/gpu-visibility-mutants $(OUT_DIR)/gpu-visibility-fs
