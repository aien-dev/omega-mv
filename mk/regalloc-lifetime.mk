# Omega #520 host-only lifetime oracle for the existing linear-scan allocator.
.PHONY: test-gpu-regalloc-lifetime
$(OUT_DIR)/gpu_regalloc_lifetime_oracle: tests/gpu_regalloc_lifetime_oracle.c src/omega_blackwell_codegen.c src/omega_blackwell_codegen.h | $(OUT_DIR)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ tests/gpu_regalloc_lifetime_oracle.c src/omega_blackwell_codegen.c

test-gpu-regalloc-lifetime: $(OUT_DIR)/gpu_regalloc_lifetime_oracle
	./$(OUT_DIR)/gpu_regalloc_lifetime_oracle
