# #622 C export lane (translation-v1 section 6): S1 upper unit -> generated C17. Author self-test only.
.PHONY: c-u64-export test-c-export
c-u64-export:
	python3 tools/adapters/build-c-export.py
test-c-export: c-u64-export
	python3 tools/adapters/build-c-import.py
	python3 tests/universal/translation/c_export_selftest.py --out build/L622/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
