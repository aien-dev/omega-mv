L705_SRC = evidence/L705-ABORT-REVIEW/embed.c $(filter-out src/osh/host/osh_main.c,$(OSH_BIN_SRCS))
$(OUT_DIR)/embed: $(L705_SRC)
	$(CC) $(OSH_XFLAGS) -O2 -DOSH_DIFF_TEST_HOOKS -o $@ $(L705_SRC)
l705-probe: $(OUT_DIR)/embed
