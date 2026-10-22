# L577 / #344: actual Bash precedence oracle, using existing compiled shell core.
.PHONY: test-osh-exit-numeric-priority
$(OSH_DIR)/osh_numeric_priority_asan: $(OSH_BIN_SRCS) $(OSH_XHDRS) src/osh/host/osh_shell.h src/osh/host/osh_priv.h
	$(CC) $(OSH_XFLAGS) $(OSC_ASAN) -o $@ $(OSH_BIN_SRCS)

test-osh-exit-numeric-priority: $(OSH_DIR)/osh $(OSH_DIR)/osh_numeric_priority_asan
	@d=$$(mktemp -d $(OSH_DIR)/numeric-priority.XXXXXX) && \
	 node tests/osh/e2e/exit_numeric_priority.mjs $(abspath $(OSH_DIR)/osh) $$d/plain > $$d/plain.log && \
	 node tests/osh/e2e/exit_numeric_priority.mjs $(abspath $(OSH_DIR)/osh_numeric_priority_asan) $$d/sanitizer > $$d/sanitizer.log && \
	 tail -1 $$d/plain.log && tail -1 $$d/sanitizer.log && echo "numeric priority artifacts: $$d"
