# #621 source-aware v2 (spec/universal/source-aware-v2.md), host/nativeCPU only.
# The v1 codec, its gate and every consumer build stay on the unchanged v1 core (src/source-aware/core.om).
.PHONY: source-aware-v2 test-source-aware-v2
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
source-aware-v2:
	@mkdir -p build/L832
	@rm -f build/L832/PASS.json
	tools/source-aware/build-v2
# Unchanged v1 gates first (they also build the frozen-v1 codec build/L574/sa, the C importer, the
# lowering and the C exporter from the v1 core), then the v2 compatibility gate against them.
test-source-aware-v2: source-aware-v2
	$(MAKE) -f mk/source-aware.mk test-source-aware
	$(MAKE) -f mk/c-u64-import.mk test-c-u64-import
	$(MAKE) -f mk/translation.mk test-translation-g1
	node tests/universal/source-aware/v2_compat.mjs
