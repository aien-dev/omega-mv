# #623 independent G1 translation validation (spec/universal/translation-v1.md). Host AArch64 only.
# Needs: the pinned omc (OMEGA_NATIVE_OMC, sha256 734f0a39...6b61), an omega-bridge clone holding
# 012860dbf26ee62a9838594b9dfd1e3e94a540a9 (OMEGA_BRIDGE_REPO), the C importer build (build/L589, from
# mk/c-u64-import.mk) and the public tools/adapters/lower and tools/adapters/c-export commands.
# A missing tool or pin gives NOT_RUN and exit 3, never PASS.
.PHONY: translation-validate-bridge test-translation-validate test-translation-compose test-source-aware-v2-reference \
  test-translation-compose-v2
T623_BUILD := build/T623
OMEGA_BRIDGE_REPO ?= $(HOME)/workspace/omega-bridge
T623_BRIDGE_COMMIT := 012860dbf26ee62a9838594b9dfd1e3e94a540a9
T623_SA := $(wildcard build/L574/sa)

# The bridge generator, compiled by the pinned omc from native.om at the pinned commit.
translation-validate-bridge:
	@test -n "$(OMEGA_NATIVE_OMC)" || { echo 'NOT_RUN set OMEGA_NATIVE_OMC to the pinned seed omc'; exit 3; }
	@mkdir -p $(T623_BUILD)
	git -C "$(OMEGA_BRIDGE_REPO)" show $(T623_BRIDGE_COMMIT):src/native.om > $(T623_BUILD)/native.om
	"$(OMEGA_NATIVE_OMC)" < $(T623_BUILD)/native.om > $(T623_BUILD)/bridge-native.tmp
	chmod 700 $(T623_BUILD)/bridge-native.tmp && mv $(T623_BUILD)/bridge-native.tmp $(T623_BUILD)/bridge-native

test-translation-validate: translation-validate-bridge
	python3 tests/universal/translation/validate_g1.py --lower tools/adapters/lower --export tools/adapters/c-export \
	  --importer build/L589/c-import --omc "$(OMEGA_NATIVE_OMC)" --bridge-native $(T623_BUILD)/bridge-native \
	  --bridge-repo "$(OMEGA_BRIDGE_REPO)" $(if $(T623_SA),--sa $(T623_SA)) \
	  --out $(T623_BUILD)/validate-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')

# G2 (spec/universal/translation-compose-v1.md): the six directed C/Rust/Python compositions and the
# same-language round trips. Needs, in addition, the Rust and Python adapter builds (mk/rust-u64.mk,
# mk/python-u64.mk), build/L574/sa, rustc and python3.
test-translation-compose: translation-validate-bridge
	python3 tests/universal/translation/validate_compose.py --omc "$(OMEGA_NATIVE_OMC)" \
	  --bridge-native $(T623_BUILD)/bridge-native --bridge-repo "$(OMEGA_BRIDGE_REPO)" --sa build/L574/sa \
	  --out $(T623_BUILD)/compose-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')

# G3 (spec/universal/source-aware-v2.md, translation-compose-v2.md). The independent v2 reference decoder
# against the v2 codec build/L832/sa (tools/source-aware/build-v2), and the 30 directed compositions of
# C/Rust/Python/C++/Zig/Go. Needs, in addition, the C++/Zig/Go adapter builds, tools/adapters/retarget,
# g++, ZIG (0.17.0) and GO (1.27.3, with gofmt beside it).
ZIG ?= $(HOME)/.local/toolchains/zig/zig
GO ?= $(HOME)/.local/toolchains/go/bin/go
test-source-aware-v2-reference:
	python3 tests/universal/source-aware/v2_reference.py --sa build/L832/sa $(if $(T623_SA),--sa-v1 $(T623_SA)) \
	  --out $(T623_BUILD)/v2-reference-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')

test-translation-compose-v2: translation-validate-bridge test-source-aware-v2-reference
	python3 tests/universal/translation/validate_compose_v2.py --omc "$(OMEGA_NATIVE_OMC)" \
	  --bridge-native $(T623_BUILD)/bridge-native --bridge-repo "$(OMEGA_BRIDGE_REPO)" --sa2 build/L832/sa \
	  --zig "$(ZIG)" --go "$(GO)" --out $(T623_BUILD)/compose-v2-$$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
