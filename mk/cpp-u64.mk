# #739 C++ lane (translation-compose-v2): cpp17-uint64-pure-v1 importer and extended-family C++ exporter.
# Author self-test only (TESTED). SA_SEED_REPO must hold bridge commit 0fc7d866 (tools/source-aware/build).
# The exporter and the codec-acceptance leg need the #621 v2 codec (src/source-aware/core-v2.om, build/L832/sa).
# Oracles: CXX (g++ 13.3.0), CC; native realization: OMEGA_NATIVE_OMC (omc 734f0a39), OMEGA_BRIDGE_REPO.
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
.PHONY: cpp-u64 test-cpp-u64
cpp-u64:
	nice -n 19 python3 tools/adapters/build-cpp-u64.py
test-cpp-u64: cpp-u64
	nice -n 19 python3 tools/adapters/build-c-import.py
	nice -n 19 python3 tools/adapters/build-c-export.py
	nice -n 19 python3 tools/adapters/build-lower.py
	nice -n 19 tools/source-aware/build-v2
	nice -n 19 python3 tests/universal/translation/cpp_selftest.py --out build/L739/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
