# #625 development-oracle contract; no production compiler changes.
.PHONY: test-universal-semantics universal-evidence-reset
UNIVERSAL_DIR = $(OUT_DIR)/universal-semantics
# Refuse stale PASS even when building the probe fails before Python runs.
universal-evidence-reset:
	python3 -c 'from pathlib import Path; p=Path("$(abspath $(UNIVERSAL_DIR))"); p.mkdir(parents=True, exist_ok=True); (p/"receipt.json").unlink(missing_ok=True)'
$(UNIVERSAL_DIR)/osc-oracle: tests/universal/osc_probe.c $(OSC_LIB) $(OSC_HDRS) | universal-evidence-reset
	@mkdir -p $(UNIVERSAL_DIR)
	$(CC) $(OSC_CFLAGS) -o $@ tests/universal/osc_probe.c $(OSC_LIB)
test-universal-semantics: $(UNIVERSAL_DIR)/osc-oracle
	python3 tests/universal/test_evidence_lifecycle.py
	CC="$(CC)" UNIVERSAL_OUT="$(abspath $(UNIVERSAL_DIR))" python3 tests/universal/semantics.py
