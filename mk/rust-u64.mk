# #626 Rust lane (translation-compose-v1): rust-u64-wrapping-pure-v1 importer and S1-family Rust exporter.
# Author self-test only (TESTED). SA_SEED_REPO must hold bridge commit 0fc7d866 (tools/source-aware/build).
# Optional: RUST_SELFTEST_C_EXPORT=<path to a passing G1 tools/adapters/c-export> runs Rust->C.
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
.PHONY: rust-u64 test-rust-u64
rust-u64:
	nice -n 19 python3 tools/adapters/build-rust-u64.py
test-rust-u64: rust-u64
	nice -n 19 python3 tests/universal/translation/rust_selftest.py --out build/L626/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
