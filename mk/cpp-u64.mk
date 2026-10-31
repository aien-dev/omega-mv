# #739 C++ lane (translation-compose-v2): cpp17-uint64-pure-v1 importer and extended-family C++ exporter.
# Author self-test only (TESTED). No default under $HOME or another checkout; required (NOT_RUN, exit 3 if unset):
#   SA_SEED_REPO       clone holding bridge commit 0fc7d866 (tools/source-aware/build, build-v2)
#   OMEGA_NATIVE_OMC   the pinned omc 734f0a39 (lowering, retarget and the native realization)
#   OMEGA_BRIDGE_REPO  clone holding omega-bridge 012860db (native.om binding generator)
# Oracles: CXX (default g++ on PATH; missing = NOT_RUN, exit 3) and CC (default cc on PATH).
# Every in-tree prerequisite is built here: the C++ adapters (build/L739), the C importer (build/L589), the
# G1 C exporter (build/L622), the lowering (build/L831), the v2 codec (build/L832) and, when the checkout has
# it, the #624 retarget tool (build/L833), which then provides R(U, M). CPP_SELFTEST_RETARGET overrides it.
.PHONY: cpp-u64 test-cpp-u64 cpp-u64-env
export SA_SEED_REPO OMEGA_NATIVE_OMC OMEGA_BRIDGE_REPO
CPP_RETARGET_BUILD := $(wildcard tools/adapters/build-retarget.py)
cpp-u64-env:
	@test -n "$(SA_SEED_REPO)" || { echo 'NOT_RUN set SA_SEED_REPO to a clone holding bridge commit 0fc7d866'; exit 3; }
cpp-u64: cpp-u64-env
	nice -n 19 python3 tools/adapters/build-cpp-u64.py
test-cpp-u64: cpp-u64
	@test -n "$(OMEGA_NATIVE_OMC)" || { echo 'NOT_RUN set OMEGA_NATIVE_OMC to the pinned omc 734f0a39'; exit 3; }
	@test -n "$(OMEGA_BRIDGE_REPO)" || { echo 'NOT_RUN set OMEGA_BRIDGE_REPO to a clone holding omega-bridge 012860db'; exit 3; }
	nice -n 19 python3 tools/adapters/build-c-import.py
	nice -n 19 python3 tools/adapters/build-c-export.py
	nice -n 19 python3 tools/adapters/build-lower.py
	nice -n 19 tools/source-aware/build-v2
	$(if $(CPP_RETARGET_BUILD),nice -n 19 python3 tools/adapters/build-retarget.py)
	nice -n 19 python3 tests/universal/translation/cpp_selftest.py --omc "$(OMEGA_NATIVE_OMC)" --bridge "$(OMEGA_BRIDGE_REPO)" \
	  --out build/L739/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
