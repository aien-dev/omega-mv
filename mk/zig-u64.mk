# #740 Zig lane (translation-compose-v2): zig-u64-wrapping-pure-v1 importer and extended-family Zig exporter.
# Author self-test only (TESTED). Everything the self-test uses is built in this tree: the v1 codec
# build/L574/sa, the v2 codec build/L832/sa, the C importer build/L589, the C exporter build/L622, the
# lowering build/L831 and, when this tree has it, the retarget tool build/L833.
# Required environment (no host-path defaults):
#   SA_SEED_REPO       omega-bridge repository holding the seed commit 0fc7d866 (tools/source-aware/build)
#   OMEGA_NATIVE_OMC   native realization compiler omc 734f0a39 (lowering, retarget, native realization)
#   OMEGA_BRIDGE_REPO  omega-bridge repository holding 012860db (native.om binding generator)
# Zig 0.17.0 comes from $ZIG or `zig` on PATH; without it the self-test exits 3 (NOT_RUN).
export SA_SEED_REPO OMEGA_NATIVE_OMC OMEGA_BRIDGE_REPO
.PHONY: zig-u64 test-zig-u64 zig-u64-env
zig-u64-env:
	@for v in SA_SEED_REPO OMEGA_NATIVE_OMC OMEGA_BRIDGE_REPO; do eval "x=\$$$$v"; [ -n "$$x" ] || { echo "ZIG_U64_MISSING_ENV: $$v must be set (absolute path)" >&2; exit 2; }; done
zig-u64: zig-u64-env
	nice -n 19 python3 tools/adapters/build-zig-u64.py
test-zig-u64: zig-u64
	$(MAKE) -f mk/source-aware.mk source-aware
	$(MAKE) -f mk/source-aware-v2.mk source-aware-v2
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	$(MAKE) -f mk/c-u64-export.mk c-u64-export
	$(MAKE) -f mk/lower.mk lower
	if [ -f mk/retarget.mk ]; then $(MAKE) -f mk/retarget.mk retarget; fi
	nice -n 19 python3 tests/universal/translation/zig_selftest.py --out build/L740/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
