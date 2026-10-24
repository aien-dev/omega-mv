# omega #699: verified state/action/next-state experience in canonical Cortex.
#
#   test-experience   walk1d family, accept chain -> CX_K_EXPERIENCE, refusal of
#                     generated-but-unobserved records, consent revocation in the
#                     dataset view (incl. restart), determinism across seeds.
#
# Not part of `all` or `test`. Host only; no GPU. Production flags (no
# AIEN_TEST_BUILD): the test uses no composition fault hooks. It links the same
# COMPOSITION-2 sources as test-composition plus rx_experience.c, rx_exp_walk.c
# and the PD0 stream primitive.
#
# This file is read before the top-level Makefile defines RX_COMPOSE_* and
# AIENOS_CAP_LIB, so prerequisites naming them are written $$(...) and expanded
# in the second phase (.SECONDEXPANSION, as mk/rx_compose_lib.mk does).
ifndef EXPERIENCE_MK
EXPERIENCE_MK := 1
.PHONY: test-experience
.SECONDEXPANSION:

RX_EXP_SRCS = src/runtime/rx_experience.c src/runtime/rx_exp_walk.c src/physics0/pd0_rng.c
RX_EXP_TEST = $(OUT_DIR)/rx_experience_test

$(RX_EXP_TEST): $$(RX_COMPOSE_DEPS) $(RX_EXP_SRCS) src/runtime/rx_experience.h \
		tests/runtime/rx_experience_test.c | $(OUT_DIR)
	$(CC) $(CFLAGS) -pthread -o $@ $(RX_COMPOSE_SRCS) $(RX_EXP_SRCS) \
		tests/runtime/rx_experience_test.c $(RX_COMPOSE_LINK)

test-experience: $(RX_EXP_TEST)
	./$(RX_EXP_TEST)

endif
