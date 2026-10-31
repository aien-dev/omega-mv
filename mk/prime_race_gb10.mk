# AIEN Prime Drag Race: native GB10 implementation (bench/prime_race/gb10_native.c)
# linked to the pinned canonical CUDA++ archive. No CUDA runtime; chip execution
# remains separate from the host build and simulation checks.
#
# prime-race-gb10            build bench/prime_race/build/gb10_native (+ _mutant, the chip test's
#                            negative control) and refuse them if nm finds a CUDA symbol
# prime-race-gb10-host-test  golden words, nvdisasm listing, host IR simulator sweep (no device)
# The chip test is bench/prime_race/tests/run_gb10_chip.sh (holds /tmp/aien-gb10.lock).
# prime-race-gb10-launch-cost  build the DIAGNOSTIC per-launch cost tool (tests/gb10_launch_cost.c;
#                            chip launches need GB10_CHIP_RUN=1 and the GPU lock)
ifndef PRIME_RACE_GB10_MK
PRIME_RACE_GB10_MK := 1
.PHONY: prime-race-gb10 prime-race-gb10-host-test prime-race-gb10-launch-cost PRIME_RACE_FORCE
PRIME_RACE_FORCE:
GB_DIR = bench/prime_race
GB_BUILD = $(GB_DIR)/build
GB_OPT = -O3 -mcpu=native
# The label carries +dirty when code that goes into the binary has uncommitted changes, and the
# stamp below changes whenever the label does, so a binary never carries a stale commit.
GB_SRC_COMMIT := $(shell git rev-parse HEAD 2>/dev/null || echo unknown)$(shell git status --porcelain -- src bench/prime_race mk 2>/dev/null | head -c1 | sed 's/.\+/+dirty/')
GB_LABEL_STAMP = $(GB_BUILD)/.gb_source_label
GB_DEFS = -DPR_SOURCE_COMMIT='"$(GB_SRC_COMMIT)"' -DPR_CC='"$(CC)"' \
	-DPR_CFLAGS='"$(GB_OPT) -I$(GB_DIR) + omega CFLAGS; pinned canonical libcudapp.a"'
GB_DEPS = $(GB_DIR)/gb10_native.c $(GB_LABEL_STAMP) $(GB_DIR)/prime_race_impl.c $(GB_DIR)/prime_race_impl.h \
	$(CUDAPP_SOURCE)/src/omega_gpu_elementwise_api.h $(CUDAPP_SOURCE)/src/omega_gpu_session.h $(CUDAPP_LIB)
# Same pattern as tools/chip_run.sh REFUSAL:cuda
GB_NM_CHECK = if nm -u $@ | grep -Eiq 'cuda|cuInit|cuLaunch|nvrtc|cublas'; then echo "CUDA symbols in $@"; rm -f $@; exit 1; fi

$(GB_BUILD)/gb10_native: $(GB_DEPS) | check-cudapp
	@mkdir -p $(GB_BUILD)
	$(CC) $(CFLAGS) $(GB_OPT) -I$(GB_DIR) $(GB_DEFS) -o $@ $(GB_DIR)/gb10_native.c $(GB_DIR)/prime_race_impl.c -lpthread -lm $(CUDAPP_LIB)
	@$(GB_NM_CHECK)
$(GB_BUILD)/gb10_native_mutant: $(GB_DEPS) | check-cudapp
	@mkdir -p $(GB_BUILD)
	$(CC) $(CFLAGS) $(GB_OPT) -I$(GB_DIR) $(GB_DEFS) -DPR_GB10_MUTANT=1 -o $@ $(GB_DIR)/gb10_native.c $(GB_DIR)/prime_race_impl.c -lpthread -lm $(CUDAPP_LIB)
	@$(GB_NM_CHECK)
prime-race-gb10: $(GB_BUILD)/gb10_native $(GB_BUILD)/gb10_native_mutant
$(GB_LABEL_STAMP): PRIME_RACE_FORCE
	@mkdir -p $(GB_BUILD)
	@echo '$(GB_SRC_COMMIT)' > $@.tmp; cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(GB_BUILD)/gb10_sieve_host_test: $(GB_DIR)/tests/gb10_sieve_host_test.c $(GB_DIR)/prime_race_impl.h $(CUDAPP_SOURCE)/src/omega_blackwell_codegen.h $(CUDAPP_SOURCE)/src/omega_gpu_elementwise_api.h $(CUDAPP_SOURCE)/tests/bw_warp_sim.h $(CUDAPP_LIB) | check-cudapp
	@mkdir -p $(GB_BUILD)
	$(CC) $(CFLAGS) -I$(GB_DIR) -I$(CUDAPP_SOURCE)/tests -Itests -o $@ $(GB_DIR)/tests/gb10_sieve_host_test.c -lpthread -lm $(CUDAPP_LIB)
prime-race-gb10-host-test: $(GB_BUILD)/gb10_sieve_host_test
	./$(GB_BUILD)/gb10_sieve_host_test
$(GB_BUILD)/gb10_launch_cost: $(GB_DIR)/tests/gb10_launch_cost.c $(CUDAPP_SOURCE)/src/omega_blackwell_codegen.h $(CUDAPP_SOURCE)/src/omega_gpu_elementwise_api.h $(CUDAPP_SOURCE)/src/omega_gpu_session.h $(CUDAPP_LIB) | check-cudapp
	@mkdir -p $(GB_BUILD)
	$(CC) $(CFLAGS) $(GB_OPT) -o $@ $(GB_DIR)/tests/gb10_launch_cost.c -lpthread -lm $(CUDAPP_LIB)
	@$(GB_NM_CHECK)
prime-race-gb10-launch-cost: $(GB_BUILD)/gb10_launch_cost
endif
