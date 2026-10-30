# #627 Python lane (translation-compose-v1): python-explicit-u64-pure-v1 importer/exporter. Author self-test only.
# SA_SEED_REPO: local clone holding the pinned bridge commit 0fc7d866 (tools/source-aware/build).
# PY_SA_ROOT: checkout whose tools/source-aware/sa is the reference #621 codec (default: this tree, built here);
# PY_C_IMPORT: the C importer used for F1 links and C->Python (default: this tree's build/L589/c-import, built here).
# PY_C_EXPORT: optional translation-v1 section 6 exporter CLI; empty = Python->C NOT_RUN.
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
PY_SA_ROOT ?= $(CURDIR)
PY_C_IMPORT ?= $(CURDIR)/build/L589/c-import
PY_C_EXPORT ?=
export SA_SEED_REPO
.PHONY: python-u64 test-python-u64
python-u64:
	python3 tools/adapters/build-python-import.py
	python3 tools/adapters/build-python-export.py
test-python-u64: python-u64
	$(MAKE) -f mk/source-aware.mk source-aware
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	python3 tests/universal/translation/python_selftest.py --sa-root $(PY_SA_ROOT) --c-import $(PY_C_IMPORT) --c-export '$(PY_C_EXPORT)' --out build/L627/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
