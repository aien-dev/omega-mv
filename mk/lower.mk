# #624 G1 lowering lane: S1 upper envelope -> restricted Omegese unit (translation-v1 section 5).
# The importer (build/L589) is built by its own unchanged target; it needs the pinned bridge seed repo.
.PHONY: lower test-lower
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
lower:
	python3 tools/adapters/build-lower.py
test-lower: lower
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	python3 tests/universal/translation/lower_selftest.py --out build/L831/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
