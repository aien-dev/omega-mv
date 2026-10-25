# #741 Go lane (translation-compose-v2): go-u64-pure-v1 importer and S1-family Go exporter.
# Author self-test only (TESTED). No host-path defaults; required environment:
#   OMEGA_BRIDGE_REPO  omega-bridge checkout holding 0fc7d866 (seed compiler) and 012860db (native binding)
#   OMEGA_NATIVE_OMC   native omc 734f0a39 (lowering realization)
#   GO, GOFMT          Go 1.27.3 toolchain, or `go`/`gofmt` on PATH (missing: the self-test exits 3 NOT_RUN)
# SA_SEED_REPO defaults to OMEGA_BRIDGE_REPO. test-go-u64 builds every in-tree prerequisite: the #621 v1 and
# v2 codecs, the C importer, the C exporter, the lowering and, when this tree holds it, the #624 retarget tool.
SA_SEED_REPO ?= $(OMEGA_BRIDGE_REPO)
export SA_SEED_REPO
.PHONY: go-u64 test-go-u64
go-u64:
	@test -n "$(SA_SEED_REPO)" || { echo 'go-u64: set OMEGA_BRIDGE_REPO (or SA_SEED_REPO)' >&2; exit 2; }
	nice -n 19 python3 tools/adapters/build-go-u64.py
test-go-u64: go-u64
	@test -n "$(OMEGA_NATIVE_OMC)" || { echo 'test-go-u64: set OMEGA_NATIVE_OMC' >&2; exit 2; }
	@test -n "$(OMEGA_BRIDGE_REPO)" || { echo 'test-go-u64: set OMEGA_BRIDGE_REPO' >&2; exit 2; }
	nice -n 19 bash tools/source-aware/build
	SA_BUILD=build/L832 nice -n 19 bash tools/source-aware/build-v2
	nice -n 19 python3 tools/adapters/build-c-import.py
	nice -n 19 python3 tools/adapters/build-c-export.py
	nice -n 19 python3 tools/adapters/build-lower.py
	if [ -f tools/adapters/build-retarget.py ]; then nice -n 19 python3 tools/adapters/build-retarget.py; fi
	nice -n 19 python3 tests/universal/translation/go_selftest.py --out build/L741/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
