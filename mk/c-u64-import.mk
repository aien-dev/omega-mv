# #622 first bounded handwritten C importer; no exporter/lowering/proof claim.
.PHONY: c-u64-import test-c-u64-import
c-u64-import:
	python3 tools/adapters/build-c-import.py
test-c-u64-import: c-u64-import
	python3 tests/universal/c_import_gate.py --importer build/L589/c-import --out build/L589/gate-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
	python3 tests/universal/c_import_controls.py --out build/L589/controls-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
