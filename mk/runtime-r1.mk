# Omega runtime R1 (omega-mv#594): the Runtime API manifest checked against the headers. Host only, no GPU, no physics checkout.
# Picked up by `-include mk/*.mk` at the top of the Makefile, so no extra include line is needed.
# The Runtime API headers, manifest, API.md and both checkers are CUDApp's: the staged source of the exact revision in
# cudapp.lock ($(CUDAPP_SOURCE), check-cudapp). Without a stage the target fails; it never falls back to a local copy.
RUNTIME_API_MANIFEST ?= $(CUDAPP_SOURCE)/docs/runtime/omega_runtime_api.manifest
RUNTIME_API_MD ?= $(CUDAPP_SOURCE)/docs/runtime/API.md
RUNTIME_API_WORK ?= $(OUT_DIR)/runtime-api
.PHONY: test-runtime-api
test-runtime-api: | check-cudapp
	CC="$(CC)" sh $(CUDAPP_SOURCE)/tools/runtime_api_check.sh $(RUNTIME_API_MANIFEST) $(CUDAPP_SOURCE) $(RUNTIME_API_WORK)/real $(RUNTIME_API_MD)
	CC="$(CC)" sh $(CUDAPP_SOURCE)/tools/runtime_api_negative.sh $(RUNTIME_API_MANIFEST) $(CUDAPP_SOURCE) $(RUNTIME_API_WORK) $(RUNTIME_API_MD)
