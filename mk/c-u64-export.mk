# #622 C export lane (translation-v1 section 6, translation-compose-v1 section 2): S1-family upper unit ->
# generated C17. Author self-test only. Optional compose leg: C_EXPORT_RUST_ROOT / C_EXPORT_PYTHON_ROOT name
# checkouts with the #626 / #627 adapters already built (their make targets); empty = that leg NOT_RUN.
C_EXPORT_RUST_ROOT ?=
C_EXPORT_PYTHON_ROOT ?=
.PHONY: c-u64-export test-c-export
c-u64-export:
	python3 tools/adapters/build-c-export.py
test-c-export: c-u64-export
	python3 tools/adapters/build-c-import.py
	python3 tests/universal/translation/c_export_selftest.py --rust-root '$(C_EXPORT_RUST_ROOT)' --python-root '$(C_EXPORT_PYTHON_ROOT)' --out build/L622/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
