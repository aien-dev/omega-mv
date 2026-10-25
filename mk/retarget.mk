# #624 G3 retarget lane: extended S1 family unit -> R(U, M) (translation-compose-v2 section 2).
# The importer (build/L589), the lowering (build/L831), the frozen v1 codec sa (build/L574, sha256 pinned
# by the self-test) and the v2 codec sa (build/L832, source-aware-v2.md section 7) are built in-tree by
# their owners' unchanged scripts. Required, no $HOME defaults: OMEGA_NATIVE_OMC (the pinned omc
# 734f0a39) and SA_SEED_REPO (a clone holding the pinned bridge seed commit).
.PHONY: retarget test-retarget retarget-env
export OMEGA_NATIVE_OMC SA_SEED_REPO
retarget-env:
	@test -n "$$OMEGA_NATIVE_OMC" || { echo 'RETARGET: set OMEGA_NATIVE_OMC to the pinned omc 734f0a39' >&2; exit 2; }
	@test -n "$$SA_SEED_REPO" || { echo 'RETARGET: set SA_SEED_REPO to a clone holding the pinned bridge seed commit' >&2; exit 2; }
retarget: retarget-env
	python3 tools/adapters/build-retarget.py
test-retarget: retarget
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	$(MAKE) -f mk/lower.mk lower
	$(MAKE) -f mk/source-aware.mk source-aware
	SA_BUILD=build/L832 tools/source-aware/build-v2
	python3 tests/universal/translation/retarget_selftest.py --out build/L833/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
