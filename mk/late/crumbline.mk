# mk/late/crumbline.mk -- the Crumbline learner (moved out of omega's Makefile with omega-programs, L396).
# Included at the end of omega's Makefile (-include mk/late/*.mk); the rules read the same variables there.

# Crumbline learner: a separate executable linked ONLY from learner-side code
# (visible reader, Omega program/realize/verify stack, synthesis vocabulary,
# OmegaLibrary, Crumbline search). It links no physics, no gate suites and no
# sealed-side code; the sealed side lives in the crumbs crate.
CL_SRCS = src/crumbline/cl_common.c src/crumbline/cl_crumb.c src/crumbline/cl_program.c src/crumbline/cl_search.c
LEARNER_CORE = sha256 omega_canonical omega_validate omega_core omega_codec aarch64_encoder aarch64_decoder \
	omega_realize omega_realize_synth omega_machine omega_exec omega_verify omega_program omega_synthesis omega_library
LEARNER_OBJS = $(addprefix $(OUT_DIR)/,$(addsuffix .o,$(LEARNER_CORE))) \
	$(patsubst src/crumbline/%.c,$(OUT_DIR)/crumbline/%.o,$(CL_SRCS)) $(OUT_DIR)/crumbline_learner.o
LEARNER = $(OUT_DIR)/crumbline-learner

.PHONY: crumbline-learner test-crumbline

$(OUT_DIR)/crumbline/%.o: src/crumbline/%.c | $(OUT_DIR)
	mkdir -p $(OUT_DIR)/crumbline
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT_DIR)/crumbline_learner.o: tools/crumbline_learner.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

crumbline-learner: $(LEARNER)

test-crumbline: $(LEARNER)
	./tests/crumbline/run_conformance.sh $(LEARNER) tests/crumbline/vectors
	./tests/crumbline/run_crb1_conformance.sh $(LEARNER) tests/crumbline/crb1 tests/crumbline/crb1_findings.txt

$(LEARNER): $(LEARNER_OBJS)
	$(CC) $(CFLAGS) -o $@.tmp.$$$$ $(LEARNER_OBJS) && mv -f $@.tmp.$$$$ $@

# VC1 stage 6: Crumbline library admissions go through the Verified Crumb bridge (VC_BRIDGE_* in omega's Makefile).
LEARNER_CORE += $(VC_BRIDGE_CORE)
$(LEARNER): $(VC_BRIDGE_OBJS)
