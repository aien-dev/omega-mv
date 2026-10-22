# #625 development-oracle contract; no production compiler changes.
.PHONY: test-universal-semantics
UNIVERSAL_DIR = $(OUT_DIR)/universal-semantics
$(UNIVERSAL_DIR)/osc-oracle: tests/universal/osc_probe.c $(OSC_LIB) $(OSC_HDRS)
	@mkdir -p $(UNIVERSAL_DIR)
	$(CC) $(OSC_CFLAGS) -o $@ tests/universal/osc_probe.c $(OSC_LIB)
test-universal-semantics: $(UNIVERSAL_DIR)/osc-oracle
	CC="$(CC)" UNIVERSAL_OUT="$(abspath $(UNIVERSAL_DIR))" python3 tests/universal/semantics.py
