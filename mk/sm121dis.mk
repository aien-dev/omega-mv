# C6 of docs/OMEGA.md: Omega's own sm_121 disassembler (src/omega_sm121_disasm.c, tools/sm121dis/).
#   make test-sm121dis   collect every kernel the library builds (no GPU), compare our text with nvdisasm's, word by word;
#                        then the red check: a decoder built with one deliberate fault must FAIL the same comparison.
# Picked up by the `-include mk/*.mk` line of the Makefile.
SM121DIS = $(OUT_DIR)/sm121dis
SM121DIS_RED = $(OUT_DIR)/sm121dis_red
SM121DIS_K = $(OUT_DIR)/sm121dis-kernels

$(SM121DIS): tools/sm121dis/sm121dis.c src/omega_sm121_disasm.c src/omega_sm121_disasm.h
	@mkdir -p $(OUT_DIR)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -Isrc -o $@ tools/sm121dis/sm121dis.c src/omega_sm121_disasm.c
$(SM121DIS_RED): tools/sm121dis/sm121dis.c src/omega_sm121_disasm.c src/omega_sm121_disasm.h
	@mkdir -p $(OUT_DIR)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -DSM121DIS_MUTANT=1 -Isrc -o $@ tools/sm121dis/sm121dis.c src/omega_sm121_disasm.c

sm121dis-corpus-bins: $(GPU_API_TEST) $(GPU_EW_TEST) $(GPU_RECONV_TEST) $(GPU_ATTN_TEST) $(GPU_MOE_TEST) $(GPU_FP8_TEST) \
	$(GPU_DATTN_TEST) $(GPU_ROUTER_TEST) $(GPU_BF16_TEST) $(GPU_TERN_TEST) $(GPU_FP8GEMM_TEST) $(GPU_TC_PROBE_TEST) \
	$(GPU_TC_PEAK_TEST) $(GPU_PF_MEM_TEST) $(GPU_QMMA_PROBE_TEST) $(GPU_CMDLIST_TEST) $(GPU_DECODE_GRAPH_TEST) \
	$(GPU_TCGEMM_TEST) $(GPU_PATTN_TEST) $(GPU_PFGEMM_TEST) $(GPU_QGEMM_TEST) $(GPU_PFHOST_TEST) $(GPU_GPR_BOUNDARY_TEST) \
	build/test_omega_divsqrt_gb10_cpu build/test_omega_ldst_gb10_cpu

.PHONY: test-sm121dis sm121dis-corpus-bins
test-sm121dis: $(SM121DIS) $(SM121DIS_RED) sm121dis-corpus-bins
	rm -rf $(SM121DIS_K) && sh tools/sm121dis/corpus.sh $(OUT_DIR) $(SM121DIS_K)
	sh tools/sm121dis/check.sh $(SM121DIS) $(SM121DIS_K) $(OUT_DIR)/sm121dis-work
	@if sh tools/sm121dis/check.sh $(SM121DIS_RED) $(SM121DIS_K) $(OUT_DIR)/sm121dis-red > $(OUT_DIR)/sm121dis-red.log 2>&1; then \
		echo "RED CHECK BROKEN: the faulty decoder passed"; exit 1; \
	else echo "red check: the faulty decoder FAILS the comparison, as it must ($$(grep -c '^\[FAIL\]' $(OUT_DIR)/sm121dis-red.log) kernels)"; fi
