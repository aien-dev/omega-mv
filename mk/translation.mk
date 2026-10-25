# #624 integration: the G1 C17 <-> Omega slice end to end (spec/universal/translation-v1.md).
# Host aarch64 NATIVE only; foreign compilers (cc) are output compilers and oracles, never a fallback.
.PHONY: translation-omc test-translation-g1
T624 := build/T624
# Native realization compiler (translation-v1 section 1): born from the seed of a pinned omega-mv commit
# unless OMEGA_NATIVE_OMC names an existing one; either way its sha256 must be 734f0a39.
T624_SEED_COMMIT := f8588e10ae70ddcb901ebf4fd20618b283a94005
T624_OMC_SHA256 := 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
OMEGA_NATIVE_OMC ?= $(CURDIR)/$(T624)/seed/seed/out/omc
SA_SEED_REPO ?= $(HOME)/workspace/omega-bridge
OMEGA_BRIDGE_REPO ?= $(SA_SEED_REPO)
export OMEGA_NATIVE_OMC SA_SEED_REPO OMEGA_BRIDGE_REPO

translation-omc:
	@if [ ! -x "$(OMEGA_NATIVE_OMC)" ] && [ "$(OMEGA_NATIVE_OMC)" = "$(CURDIR)/$(T624)/seed/seed/out/omc" ]; then \
	  rm -rf $(T624)/seed && mkdir -p $(T624)/seed && \
	  git archive $(T624_SEED_COMMIT) seed | tar -x -C $(T624)/seed && \
	  (cd $(T624)/seed && sh seed/omega-from-seed.sh > ../seed.log 2>&1) || { tail -3 $(T624)/seed.log; exit 1; }; fi
	@test "$$(sha256sum < "$(OMEGA_NATIVE_OMC)" | cut -c1-64)" = "$(T624_OMC_SHA256)" || \
	  { echo "T624: $(OMEGA_NATIVE_OMC) is not the pinned omc $(T624_OMC_SHA256)"; exit 1; }

test-translation-g1: translation-omc
	python3 tests/universal/translation/make_fixtures.py --check
	$(MAKE) -f mk/c-u64-import.mk test-c-u64-import
	$(MAKE) -f mk/source-aware.mk source-aware
	$(MAKE) -f mk/lower.mk test-lower
	$(MAKE) -f mk/c-u64-export.mk test-c-export
	$(MAKE) -f mk/translation-validate.mk test-translation-validate
	@echo "TRANSLATION-G1 PASS (fixtures, C import, codec, lower, C export, independent validation)"
