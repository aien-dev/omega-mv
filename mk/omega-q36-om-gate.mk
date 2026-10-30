# The Omegese decode gate's host checks (src/omega_gpu_q36_graph.c om_gate_load, om_gate_check; omega-mv#886): a byte of
# the embedded gate code flipped is refused before any allocation, the bytes as embedded load, and read_head fails on a
# trap word. Host only: no device, no physics headers (the test stands in for the session calls; --gc-sections drops
# the graph code it does not reach; separate-code keeps the embedded bytes off the text pages the test makes writable).
# Two source mutants, each a copy of the graph source with one line deleted, must fail it; a site that is not found
# exactly once is an error, not a kill. Run by tests/gates/run.sh numeric-cpu.
Q36_OM_GATE_CC := gcc -std=gnu11 -O2 -Wall -Wextra -Werror -D_GNU_SOURCE -ffunction-sections -fdata-sections -Isrc
Q36_OM_GATE_LD := -Wl,--gc-sections -Wl,-z,separate-code
Q36_OM_GATE_LIBS := src/omega_om_units.c src/omega_gpu_fmt.c
Q36_OM_GATE_DEPS := tests/test_omega_q36_om_gate.c src/omega_gpu_q36_graph.c src/omega_q36_om_gate.h $(Q36_OM_GATE_LIBS)
# mutant name = the line it deletes
Q36_OM_GATE_M_NO_SHA := if (strcmp(s, OMEGA_Q36_OM_GATE_CODE_SHA256) != 0) return OMEGA_GPU_DGRAPH_CODEGEN_FAIL;
Q36_OM_GATE_M_NO_READ_CHECK := if (rc == OMEGA_GPU_DGRAPH_OK) rc = om_gate_check(g); /* a trap word fails the read too
.PHONY: test-q36-om-gate-host
build/test_omega_q36_om_gate: $(Q36_OM_GATE_DEPS)
	@mkdir -p build
	$(Q36_OM_GATE_CC) -o $@ tests/test_omega_q36_om_gate.c $(Q36_OM_GATE_LIBS) $(Q36_OM_GATE_LD)
test-q36-om-gate-host: build/test_omega_q36_om_gate
	./build/test_omega_q36_om_gate
	@set -e; for m in NO_SHA NO_READ_CHECK; do \
	  case $$m in NO_SHA) site='$(Q36_OM_GATE_M_NO_SHA)';; NO_READ_CHECK) site='$(Q36_OM_GATE_M_NO_READ_CHECK)';; esac; \
	  n=$$(grep -cF "$$site" src/omega_gpu_q36_graph.c || true); \
	  if [ "$$n" != 1 ]; then echo "mutant $$m: site found $$n times in src/omega_gpu_q36_graph.c"; exit 1; fi; \
	  grep -vF "$$site" src/omega_gpu_q36_graph.c > build/q36_om_gate_$$m.c; \
	  $(Q36_OM_GATE_CC) -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function \
	    -DOM_GATE_GRAPH_SRC='"$(CURDIR)/build/q36_om_gate_'$$m'.c"' -o build/test_omega_q36_om_gate_$$m \
	    tests/test_omega_q36_om_gate.c $(Q36_OM_GATE_LIBS) $(Q36_OM_GATE_LD); \
	  if ./build/test_omega_q36_om_gate_$$m >build/test_omega_q36_om_gate_$$m.log 2>&1; then echo "MUTANT SURVIVED: $$m"; exit 1; fi; \
	  echo "mutant $$m: killed ($$(grep -c '^FAIL:' build/test_omega_q36_om_gate_$$m.log) failing checks)"; \
	done
	@echo "OMEGA_Q36_OM_GATE_HOST PASS"
