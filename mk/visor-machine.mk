# Omega Visor V1 lane 5 (machine + realization lab) test targets.
# The Blackwell realization implementation comes from the canonical archive.
# Its identity is a weak reference in visor_machine.c, so explicitly retain that
# member for these tests, matching their former explicit local-object linkage.
VISOR_BW_LINK = -Wl,--undefined=omega_blackwell_get_machine_id

$(OUT_DIR)/tests-visor/test_visor_machine: tests/visor/test_visor_machine.c $(VISOR_CORE_OBJS) $(VISOR_OBJS) $(LANG_OBJS) $(CUDAPP_LIB) | $(OUT_DIR)/tests-visor check-cudapp
	$(CC) $(VISOR_CFLAGS) -Itests/visor -o $@ $< $(VISOR_CORE_OBJS) $(VISOR_OBJS) $(LANG_OBJS) $(VISOR_BW_LINK) $(CUDAPP_LIB)

$(OUT_DIR)/tests-visor/test_visor_realization: tests/visor/test_visor_realization.c $(VISOR_CORE_OBJS) $(VISOR_OBJS) $(LANG_OBJS) $(CUDAPP_LIB) | $(OUT_DIR)/tests-visor check-cudapp
	$(CC) $(VISOR_CFLAGS) -Itests/visor -o $@ $< $(VISOR_CORE_OBJS) $(VISOR_OBJS) $(LANG_OBJS) $(VISOR_BW_LINK) $(CUDAPP_LIB)

.PHONY: test-visor-machine test-visor-realization
test-visor-machine: $(OUT_DIR)/tests-visor/test_visor_machine
	./$(OUT_DIR)/tests-visor/test_visor_machine

test-visor-realization: $(OUT_DIR)/tests-visor/test_visor_realization
	./$(OUT_DIR)/tests-visor/test_visor_realization
