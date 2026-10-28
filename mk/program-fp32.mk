# FP32 in the program IR (spec/program-fp32.md, E1 gap row 12). Physics-free, host-only.
PROGRAM_FP32_TEST = $(OUT_DIR)/tests-realize/test_program_fp32
PROGRAM_FP32_NUM = src/omega_numeric.c src/omega_numeric_provenance.c

$(PROGRAM_FP32_TEST): tests/realize/test_program_fp32.c src/omega_program_fp32.c src/omega_program_fp32.h $(VISOR_CORE_OBJS) $(PROGRAM_FP32_NUM) $(CUDAPP_LIB) | check-cudapp
	mkdir -p $(dir $@)
	$(CC) $(VISOR_CFLAGS) $(CUDAPP_HEADER_FLAGS) -ffp-contract=off -DOMEGA_NUMERIC_CPU_ONLY -o $@ tests/realize/test_program_fp32.c src/omega_program_fp32.c $(PROGRAM_FP32_NUM) $(VISOR_CORE_OBJS) $(CUDAPP_LIB)

.PHONY: test-program-fp32
test-program-fp32: $(PROGRAM_FP32_TEST)
	./$(PROGRAM_FP32_TEST)
