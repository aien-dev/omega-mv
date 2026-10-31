# Omega runtime R3 (omega-mv#596): the portability table must match the source tree.
# Picked up by `-include mk/*.mk`. Host only: no compiler, no device, no physics headers.
# The GPU sources, their table (docs/runtime/portability_table.tsv), the freestanding list (mk/omega-gpu-engine.mk) and
# both scripts are CUDApp's: the staged source of the exact revision in cudapp.lock ($(CUDAPP_SOURCE), check-cudapp).
# Omega's own docs/runtime/portability_table.tsv holds only the rows of the GPU-named files Omega still carries
# (src/omega_blackwell_gates.*), checked by the same canonical script against this tree and this mk/omega-gpu-engine.mk.
#   test-runtime-portability-check     the canonical tree against the canonical table, then Omega's tree against Omega's
#                                      table: every src/omega_gpu_*, omega_blackwell_*, omega_bw_* file has a row, every
#                                      row names a file, a PORTABLE row's file includes no Linux/NVRM or Blackwell header,
#                                      and the os_binding column agrees with the include scan and the freestanding list
#   test-runtime-portability-controls  breaks one thing at a time in scratch copies of the canonical tree; the check must
#                                      FAIL each (and PASS the guarded-include control)
#   test-runtime-portability           both
.PHONY: test-runtime-portability test-runtime-portability-check test-runtime-portability-controls
test-runtime-portability-check: | check-cudapp
	sh $(CUDAPP_SOURCE)/tools/runtime_portability_check.sh $(CUDAPP_SOURCE) $(CUDAPP_SOURCE)/docs/runtime/portability_table.tsv \
		$(CUDAPP_SOURCE)/mk/omega-gpu-engine.mk
	sh $(CUDAPP_SOURCE)/tools/runtime_portability_check.sh . docs/runtime/portability_table.tsv mk/omega-gpu-engine.mk
test-runtime-portability-controls: | check-cudapp
	cd $(CUDAPP_SOURCE) && sh tools/runtime_portability_controls.sh $(abspath $(OUT_DIR)/runtime_r3_controls) \
		docs/runtime/portability_table.tsv mk/omega-gpu-engine.mk
test-runtime-portability: test-runtime-portability-check test-runtime-portability-controls
