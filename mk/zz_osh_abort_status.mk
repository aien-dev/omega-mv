# L611: status of a resumable list after a parent exit count error.
.PHONY: test-osh-exit-abort-status test-osh-exit-abort-mutants

test-osh-exit-abort-status: $(OSH_DIR)/osh $(OSH_DIR)/osh_numeric_priority_asan
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-status.XXXXXX); \
	 node tests/osh/e2e/exit_abort_status.mjs $(abspath $(OSH_DIR)/osh) $$d/plain > $$d/plain.log; \
	 node tests/osh/e2e/exit_abort_status.mjs $(abspath $(OSH_DIR)/osh_numeric_priority_asan) $$d/sanitizer > $$d/sanitizer.log; \
	 node tests/osh/e2e/exit_independent_601.mjs $(abspath $(OSH_DIR)/osh) $$d/independent-plain > $$d/independent-plain.log; \
	 node tests/osh/e2e/exit_independent_601.mjs $(abspath $(OSH_DIR)/osh_numeric_priority_asan) $$d/independent-sanitizer > $$d/independent-sanitizer.log; \
	 python3 tests/osh/host/exit_abort_pty.py $(abspath $(OSH_DIR)/osh) $$d/pty > $$d/pty.log; \
	 for f in $$d/*.log; do tail -1 $$f; done; echo "abort status artifacts: $$d"

test-osh-exit-abort-mutants: $(OSH_DIR)/osh
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-status-mutants.XXXXXX); \
	 node tests/osh/e2e/exit_abort_status.mjs $(abspath $(OSH_DIR)/osh) $$d/reference > $$d/reference.log; \
	 node tests/osh/e2e/exit_abort_status_mutants.mjs $$d/mutants $$d/reference/results.json > $$d/mutants.log; \
	 cat $$d/mutants.log; echo "abort control artifacts: $$d"
