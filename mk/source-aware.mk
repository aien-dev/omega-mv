# #621 host/nativeCPU only; external C/Node are independent test oracles.
.PHONY: source-aware source-aware-reset test-source-aware
source-aware-reset:
	@mkdir -p build/L574
	@rm -f build/L574/PASS.json
source-aware: source-aware-reset
	tools/source-aware/build
test-source-aware: source-aware
	node tests/universal/source-aware/gate.mjs
