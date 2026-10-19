# omega-mv#584 (ledger L048): bounded learning-mechanism search harness.
# Additive research harness: links the Visor core object set (realization path) and
# the canonical Cortex store for persistence. Executes generated code natively (AArch64).
LS584_TEST = $(OUT_DIR)/tests-research/ls584

$(LS584_TEST): tests/research/ls584/ls584.c $(VISOR_CORE_OBJS) $(OUT_DIR)/rx_cortex.o
	mkdir -p $(dir $@)
	$(CC) $(VISOR_CFLAGS) -o $@ $< $(VISOR_CORE_OBJS) $(OUT_DIR)/rx_cortex.o

.PHONY: ls584
ls584: $(LS584_TEST)
