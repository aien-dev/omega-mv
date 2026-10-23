# mk/late/turing.mk -- TURING Field v1 and the TURING-selection binary of the MIXED_ALGEBRA_DIGITAL_V1 closure
# gate (moved out of omega's Makefile with omega-programs, L396). Included at the end of omega's Makefile.

# TURING Wave 1 (docs/turing/TURING_W0_PROPOSAL.md): Field v1 records (K.7) +
# control-arm selector, post hoc over evidence/MIXED_ALGEBRA receipts.
# Reads src/algebra (registry) without modifying it; no runtime, no timed runs.
# test-turing: plain + ASan/UBSan suites, then a rebuild check (spec ids from
# two different builds must be byte-identical).
.PHONY: test-turing turing-field
TURING_SRCS = src/turing/field.c src/turing/field_select.c src/turing/field_select_v0_retired.c src/turing/history_selector.c \
	src/turing/replay.c src/omega_canonical.c src/sha256.c $(OMA_RZ_SRCS)
TURING_HDRS = src/turing/field.h src/turing/select.h src/omega_canonical.h src/omega_types.h src/sha256.h $(OMA_RZ_HDRS)
TURING_ASAN = -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
TURING_TEST = $(OUT_DIR)/tests-turing/test_turing
TURING_TEST_ASAN = $(OUT_DIR)/tests-turing/test_turing_asan
TURING_TOOL = $(OUT_DIR)/tests-turing/turing-field
TURING_TOOL_ASAN = $(OUT_DIR)/tests-turing/turing-field_asan

$(TURING_TEST): tests/turing/test_turing.c $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) -o $@ tests/turing/test_turing.c $(TURING_SRCS) -lm

$(TURING_TEST_ASAN): tests/turing/test_turing.c $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) $(TURING_ASAN) -o $@ tests/turing/test_turing.c $(TURING_SRCS) -lm

$(TURING_TOOL): tools/turing_field.c $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) -o $@ tools/turing_field.c $(TURING_SRCS) -lm

$(TURING_TOOL_ASAN): tools/turing_field.c $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) $(TURING_ASAN) -o $@ tools/turing_field.c $(TURING_SRCS) -lm

turing-field: $(TURING_TOOL)
	./$(TURING_TOOL)

test-turing: $(TURING_TEST) $(TURING_TEST_ASAN) $(TURING_TOOL) $(TURING_TOOL_ASAN)
	./$(TURING_TEST)
	./$(TURING_TEST_ASAN)
	./$(TURING_TOOL) --spec-ids > $(OUT_DIR)/tests-turing/spec_ids_plain.txt
	./$(TURING_TOOL_ASAN) --spec-ids > $(OUT_DIR)/tests-turing/spec_ids_asan.txt
	cmp $(OUT_DIR)/tests-turing/spec_ids_plain.txt $(OUT_DIR)/tests-turing/spec_ids_asan.txt
	./$(TURING_TOOL_ASAN) > $(OUT_DIR)/tests-turing/turing_field_asan.txt
	@echo "test-turing: spec ids identical across plain and ASan builds; turing-field runs clean under ASan/UBSan"

# MIXED_ALGEBRA_DIGITAL_V1 closure gate (spec/mixed-algebra-digital-v1.md): the binary that runs the
# TURING Field v1 selection, and the gate itself. test-ma-digital-bottom and DV1_BOTTOM stay in omega.
.PHONY: gate-mixed-algebra-digital-v1
DV1_GATE_BIN = $(OUT_DIR)/tests-algebra/ma_digital_v1_gate
DV1_GATE_BIN_ASAN = $(OUT_DIR)/tests-algebra/ma_digital_v1_gate_asan

$(DV1_GATE_BIN): tests/algebra/ma_digital_v1_gate.c $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) -o $@ tests/algebra/ma_digital_v1_gate.c $(TURING_SRCS) -lm

$(DV1_GATE_BIN_ASAN): tests/algebra/ma_digital_v1_gate.c $(TURING_SRCS) $(TURING_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(OMA_RZ_CFLAGS) $(TURING_ASAN) -o $@ tests/algebra/ma_digital_v1_gate.c $(TURING_SRCS) -lm

gate-mixed-algebra-digital-v1: $(DV1_GATE_BIN) $(DV1_GATE_BIN_ASAN) $(DV1_BOTTOM) $(DV1_BOTTOM_ASAN)
	DV1_GATE=./$(DV1_GATE_BIN) DV1_GATE_ASAN=./$(DV1_GATE_BIN_ASAN) MAKE="$(MAKE)" \
		sh tests/algebra/ma_digital_v1_gate.sh
