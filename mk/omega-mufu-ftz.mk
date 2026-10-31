# MUFU flush rule (omega #462): the GB10 MUFU unit (EX2 / RCP / RSQ) flushes a subnormal input or result to a
# sign-preserving zero. $(CUDAPP_SOURCE)/tests/bw_mufu_ftz.h is the one host model of it; every warp simulator and SwiGLU host reference
# routes through it. test-mufu-ftz runs the helper unit test, then one MUFU instruction through each simulator's
# ws_lane_exec (bw_warp_sim.h, _k, _pa, _tc). Picked up by `-include mk/*.mk`. Host only: opens no device, needs no
# physics headers, takes no chip lock. The attention test's own per-thread simulator is checked by
# `gpu_attention_test --host-only` (test-gpu-attention).
MUFU_FTZ_SIMS := bw_warp_sim bw_warp_sim_k bw_warp_sim_pa bw_warp_sim_tc
MUFU_FTZ_CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -ffp-contract=off -Isrc -I$(CUDAPP_SOURCE)/tests -Itests
.PHONY: test-mufu-ftz
$(OUT_DIR)/test_mufu_ftz_helper: $(CUDAPP_SOURCE)/tests/bw_mufu_ftz_test.c $(CUDAPP_SOURCE)/tests/bw_mufu_ftz.h
	@mkdir -p $(OUT_DIR)
	gcc $(MUFU_FTZ_CFLAGS) -o $@ $(CUDAPP_SOURCE)/tests/bw_mufu_ftz_test.c -lm
$(OUT_DIR)/test_mufu_ftz_%: $(CUDAPP_SOURCE)/tests/bw_mufu_ftz_test.c $(CUDAPP_SOURCE)/tests/bw_mufu_ftz.h tests/%.h $(CUDAPP_SOURCE)/tests/bw_hmma_model.h $(CUDAPP_SOURCE)/tests/bw_qmma_model.h $(CUDAPP_SOURCE)/src/omega_blackwell_codegen.h
	@mkdir -p $(OUT_DIR)
	gcc $(MUFU_FTZ_CFLAGS) -DWS_SIM_HDR='"$*.h"' -o $@ $(CUDAPP_SOURCE)/tests/bw_mufu_ftz_test.c -lm
test-mufu-ftz: $(OUT_DIR)/test_mufu_ftz_helper $(addprefix $(OUT_DIR)/test_mufu_ftz_,$(MUFU_FTZ_SIMS))
	$(OUT_DIR)/test_mufu_ftz_helper
	@for s in $(MUFU_FTZ_SIMS); do $(OUT_DIR)/test_mufu_ftz_$$s || exit 1; done
	@echo "test-mufu-ftz: PASS (helper + $(words $(MUFU_FTZ_SIMS)) simulators)"
