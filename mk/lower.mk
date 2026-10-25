# #624 G1 lowering lane: S1 upper envelope -> restricted Omegese unit (translation-v1 section 5).
# The importer (build/L589) is built by its own unchanged target. Required, no $HOME defaults:
# OMEGA_NATIVE_OMC (the pinned omc 734f0a39) and SA_SEED_REPO (a clone holding the pinned bridge seed
# commit); OMEGA_BRIDGE_REPO (a clone holding omega-bridge 012860db) defaults to SA_SEED_REPO.
.PHONY: lower test-lower lower-env
OMEGA_BRIDGE_REPO ?= $(SA_SEED_REPO)
export OMEGA_NATIVE_OMC SA_SEED_REPO OMEGA_BRIDGE_REPO
lower-env:
	@test -n "$$OMEGA_NATIVE_OMC" || { echo 'LOWER: set OMEGA_NATIVE_OMC to the pinned omc 734f0a39' >&2; exit 2; }
	@test -n "$$SA_SEED_REPO" || { echo 'LOWER: set SA_SEED_REPO to a clone holding the pinned bridge seed commit' >&2; exit 2; }
lower: lower-env
	python3 tools/adapters/build-lower.py
test-lower: lower
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	python3 tests/universal/translation/lower_selftest.py --out build/L831/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
