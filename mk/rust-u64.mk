# #626 Rust lane (translation-compose-v1): rust-u64-wrapping-pure-v1 importer and S1-family Rust exporter.
# Author self-test only (TESTED). SA_SEED_REPO must hold bridge commit 0fc7d866 (tools/source-aware/build).
# Everything the self-test needs is built in-tree: build/L574/sa (mk/source-aware.mk) and
# build/L589/c-import (mk/c-u64-import.mk). Overrides: RUST_SA_ROOT (checkout with tools/source-aware/sa and
# build/L574/sa), RUST_C_IMPORT (native C importer), RUST_C_EXPORT (public tools/adapters/c-export of a
# passing G1 checkout; empty = Rust->C reported NOT_RUN).
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
RUST_SA_ROOT ?= $(CURDIR)
RUST_C_IMPORT ?= $(CURDIR)/build/L589/c-import
RUST_C_EXPORT ?=
.PHONY: rust-u64 test-rust-u64
rust-u64:
	nice -n 19 python3 tools/adapters/build-rust-u64.py
test-rust-u64: rust-u64
	$(MAKE) -f mk/source-aware.mk source-aware
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	nice -n 19 python3 tests/universal/translation/rust_selftest.py --sa-root '$(RUST_SA_ROOT)' --c-import '$(RUST_C_IMPORT)' --c-export '$(RUST_C_EXPORT)' --out build/L626/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
