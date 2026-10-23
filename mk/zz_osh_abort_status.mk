# L611: independently bounded, complete status-regression phases.
.PHONY: test-osh-exit-abort-status test-osh-exit-abort-mutants test-osh-exit-abort-focused-plain test-osh-exit-abort-focused-sanitizer test-osh-exit-abort-independent-plain test-osh-exit-abort-independent-sanitizer test-osh-exit-abort-pty test-osh-exit-abort-coverage

test-osh-exit-abort-status: test-osh-exit-abort-coverage test-osh-exit-abort-focused-plain test-osh-exit-abort-focused-sanitizer test-osh-exit-abort-independent-plain test-osh-exit-abort-independent-sanitizer test-osh-exit-abort-pty

test-osh-exit-abort-coverage:
	python3 tests/osh/e2e/independent_601_coverage.py --self-test

test-osh-exit-abort-focused-plain: $(OSH_DIR)/osh
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-focused-plain.XXXXXX); node tests/osh/e2e/exit_abort_status.mjs $(abspath $(OSH_DIR)/osh) $$d/cases > $$d/run.log; cat $$d/run.log

test-osh-exit-abort-focused-sanitizer: $(OSH_DIR)/osh_numeric_priority_asan
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-focused-asan.XXXXXX); node tests/osh/e2e/exit_abort_status.mjs $(abspath $(OSH_DIR)/osh_numeric_priority_asan) $$d/cases > $$d/run.log; cat $$d/run.log

test-osh-exit-abort-independent-plain: $(OSH_DIR)/osh
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-independent-plain.XXXXXX); python3 tests/osh/e2e/independent_601_parallel.py $(abspath $(OSH_DIR)/osh) $$d/cases > $$d/run.log; cat $$d/run.log

test-osh-exit-abort-independent-sanitizer: $(OSH_DIR)/osh_numeric_priority_asan
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-independent-asan.XXXXXX); python3 tests/osh/e2e/independent_601_parallel.py $(abspath $(OSH_DIR)/osh_numeric_priority_asan) $$d/cases > $$d/run.log; cat $$d/run.log

test-osh-exit-abort-pty: $(OSH_DIR)/osh
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-pty.XXXXXX); python3 tests/osh/host/exit_abort_pty.py $(abspath $(OSH_DIR)/osh) $$d/cases > $$d/run.log; cat $$d/run.log

test-osh-exit-abort-mutants: $(OSH_DIR)/osh
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/abort-status-mutants.XXXXXX); \
	 node tests/osh/e2e/exit_abort_status.mjs $(abspath $(OSH_DIR)/osh) $$d/reference > $$d/reference.log; \
	 node tests/osh/e2e/exit_abort_status_mutants.mjs $$d/mutants $$d/reference/results.json > $$d/mutants.log; \
	 cat $$d/mutants.log; echo "abort control artifacts: $$d"
