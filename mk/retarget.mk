# #624 G3 retarget lane: extended S1 family unit -> R(U, M) (translation-compose-v2 section 2).
# The importer (build/L589), the lowering (build/L831) and the v2 codec sa (build/L832, source-aware-v2.md
# section 7) are built by their owners' unchanged scripts; the importer and codec need the pinned bridge seed repo.
.PHONY: retarget test-retarget
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
export SA_SEED_REPO
retarget:
	python3 tools/adapters/build-retarget.py
test-retarget: retarget
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	$(MAKE) -f mk/lower.mk lower
	SA_BUILD=build/L832 tools/source-aware/build-v2
	python3 tests/universal/translation/retarget_selftest.py --out build/L833/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
