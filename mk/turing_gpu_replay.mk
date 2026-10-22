# #522 additive host oracle. C is restricted to the existing host/testing role.
TGR_TEST := $(OUT_DIR)/tests-turing/gpu_replay
TGR_SOURCES := tests/turing/test_gpu_replay.c tools/turing_gpu_replay.c
TGR_HEADERS := tools/turing_gpu_replay.h
$(TGR_TEST): $(TGR_SOURCES) $(TGR_HEADERS) $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) -Itools -o $@ $(TGR_SOURCES) $(TURING_SRCS) -lm
.PHONY: test-turing-gpu-replay
test-turing-gpu-replay: $(TGR_TEST)
	$(TGR_TEST)
$(TGR_TEST)_asan: $(TGR_SOURCES) $(TGR_HEADERS) $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) $(TURING_ASAN) -Itools -o $@ $(TGR_SOURCES) $(TURING_SRCS) -lm
.PHONY: test-turing-gpu-replay-sanitize
test-turing-gpu-replay-sanitize: $(TGR_TEST)_asan
	$(TGR_TEST)_asan
.PHONY: test-turing-gpu-replay-mutants
test-turing-gpu-replay-mutants:
	sh tests/turing/gpu_replay_mutants.sh $(OUT_DIR)/gpu-replay-mutants
