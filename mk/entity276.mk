# aien-dev/aien-architecture-mv#276 / omega-mv#584 entity pilot (ledger L058).
# Additive research harness: calls the World, Cortex, plan cache and visor World view;
# edits none of them. Same objects and flags as test-plan-reuse and test-visor-world.
ENTITY276_SRCS = src/runtime/rx_caproot.c src/runtime/rx_world.c src/runtime/rx_coherent.c \
	src/runtime/rx_native_bind.c src/runtime/rx_aegis.c src/runtime/rx_cortex_record.c \
	src/sha256.c src/omega_evidence.c src/omega_core.c src/omega_canonical.c \
	src/visor/rx/visor_world_rx.c tests/research/entity276/entity276.c
ENTITY276_TEST = $(OUT_DIR)/tests-research/entity276

# mk/*.mk is read before the Makefile defines RX_PLAN_OBJS, RX_GRAPH_OBJ and AIENOS_CAP_LIB,
# so the shared objects are built by a sub-make (recipes expand late).
.PHONY: entity276-deps
entity276-deps:
	$(MAKE) --no-print-directory $(RX_PLAN_OBJS) $(RX_GRAPH_OBJ) $(OUT_DIR)/rx_cortex.o \
		$(OUT_DIR)/visor/visor_world.o $(AIENOS_CAP_LIB)

$(ENTITY276_TEST): $(ENTITY276_SRCS) entity276-deps src/visor/visor_world.h \
		src/runtime/rx_plan.h src/runtime/rx_cortex.h src/runtime/rx_cortex_record.h
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isrc/visor -pthread -o $@ $(ENTITY276_SRCS) $(RX_PLAN_OBJS) $(RX_GRAPH_OBJ) \
		$(OUT_DIR)/rx_cortex.o $(OUT_DIR)/visor/visor_world.o $(AIENOS_CAP_LIB) -lm

.PHONY: entity276 test-entity276
entity276: $(ENTITY276_TEST)
test-entity276: $(ENTITY276_TEST)
	ENTITY276_BIN=$(ENTITY276_TEST) PILOT_OUT=$(OUT_DIR)/entity276-out bash tests/research/entity276/run.sh
