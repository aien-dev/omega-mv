# #740 Zig lane (translation-compose-v2): zig-u64-wrapping-pure-v1 importer and extended-family Zig exporter.
# Author self-test only (TESTED). SA_SEED_REPO must hold bridge commit 0fc7d866 (tools/source-aware/build).
# The v2 codec (build/L832/sa) is built when tools/source-aware/build-v2 is in the tree; otherwise the codec
# acceptance checks report NOT_RUN. Optional: ZIG_SELFTEST_C_EXPORT, ZIG_SELFTEST_LOWER, OMEGA_NATIVE_OMC,
# OMEGA_BRIDGE_REPO (G1 C exporter and native realization; defaults in the self-test).
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
.PHONY: zig-u64 test-zig-u64
zig-u64:
	nice -n 19 python3 tools/adapters/build-zig-u64.py
	if [ -f tools/source-aware/build-v2 ]; then nice -n 19 bash tools/source-aware/build-v2; fi
test-zig-u64: zig-u64
	nice -n 19 python3 tests/universal/translation/zig_selftest.py --out build/L740/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
