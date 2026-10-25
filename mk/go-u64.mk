# #741 Go lane (translation-compose-v2): go-u64-pure-v1 importer and S1-family Go exporter.
# Author self-test only (TESTED). SA_SEED_REPO must hold bridge commit 0fc7d866 (tools/source-aware/build).
# test-go-u64 also builds the #621 v2 codec (build/L832/sa) and the G1 lowering (build/L831/lower) of this
# checkout. Native realization needs omc 734f0a39 (OMEGA_NATIVE_OMC) and omega-bridge 012860db
# (OMEGA_BRIDGE_REPO); Go->C needs a passing #622 public c-export (GO_SELFTEST_C_EXPORT). A missing piece is
# reported NOT_RUN, never PASS. GO_SELFTEST_RETARGET=<public tools/adapters/retarget of a scratch checkout
# merging #624 retarget> replaces the Python R in the compositions, cross-checked byte for byte against it.
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
.PHONY: go-u64 test-go-u64
go-u64:
	nice -n 19 python3 tools/adapters/build-go-u64.py
test-go-u64: go-u64
	nice -n 19 bash tools/source-aware/build-v2
	nice -n 19 python3 tools/adapters/build-lower.py
	nice -n 19 python3 tests/universal/translation/go_selftest.py --out build/L741/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
