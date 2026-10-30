# #624 S2 (spec/universal/translation-s2-v1.md): signed i64, comparisons and select. Host AArch64 only.
# Builds build/S2 (v3 codec sa, c-i64-import, rust-i64-export with the adapter pin 191b776e; lower-s2 with the
# native pin 734f0a39) and runs the section 6 self-test: positive, boundary, overflow (gcc UBSan and rustc
# debug oracles), malformed, unsupported, mutants, the C -> Omega -> Rust composition and native execution
# through the pinned omega-bridge generator. Inputs as mk/translation.mk: SA_SEED_REPO (absolute
# omega-bridge clone path, required), optional OMEGA_NATIVE_OMC. S2_LOG_DIR also receives the receipt.
# A missing oracle or pin is NOT_RUN (exit 3), never PASS. S1 builds and gates are not touched.
.PHONY: translation-s2 test-translation-s2
OMEGA_NATIVE_OMC ?= $(CURDIR)/build/T624/seed/seed/out/omc
OMEGA_BRIDGE_REPO ?= $(SA_SEED_REPO)
export OMEGA_NATIVE_OMC SA_SEED_REPO OMEGA_BRIDGE_REPO
S2_LOG_DIR ?=
S2_CC ?= gcc
S2_RUSTC ?= rustc

translation-s2:
	$(MAKE) -f mk/translation.mk translation-omc
	nice -n 19 python3 tools/adapters/build-s2.py

test-translation-s2: translation-s2
	$(MAKE) -f mk/source-aware-v2.mk source-aware-v2
	$(MAKE) -f mk/c-u64-import.mk c-u64-import
	$(MAKE) -f mk/translation-validate.mk translation-validate-bridge
	nice -n 19 python3 tests/universal/translation/s2_selftest.py --omc "$(OMEGA_NATIVE_OMC)" \
	  --omc191 build/L574/toolchain/build/bootstrap/omc --bridge-native build/T623/bridge-native \
	  --sa2 build/L832 --s2build build/S2 --c-import-v1 build/L589/c-import --cc "$(S2_CC)" --rustc "$(S2_RUSTC)" \
	  $(if $(S2_LOG_DIR),--log-dir "$(S2_LOG_DIR)") \
	  --out build/S2/selftest-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
