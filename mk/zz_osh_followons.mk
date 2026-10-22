# Module qualification wiring; loaded after the unchanged imported OSH rules.
.PHONY: test-osh-exit-arguments test-osh-reader-boundaries test-osh-followon-mutants

test-osh-exit-arguments: $(OSH_DIR)/osh $(OSH_DIR)/osh_numeric_priority_asan
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/exit-arguments.XXXXXX); \
	 node tests/osh/e2e/exit_argument_completion.mjs $(abspath $(OSH_DIR)/osh) $$d/plain > $$d/plain.log; \
	 node tests/osh/e2e/exit_argument_completion.mjs $(abspath $(OSH_DIR)/osh_numeric_priority_asan) $$d/sanitizer > $$d/sanitizer.log; \
	 tail -1 $$d/plain.log; tail -1 $$d/sanitizer.log; echo "argument artifacts: $$d"

test-osh-reader-boundaries: $(OSH_DIR)/osh_diff $(OSH_DIR)/osh_diff_asan $(OSH_DIR)/osh_toy
	@set -eu; d=$$(mktemp -d $(OSH_DIR)/reader-boundaries.XXXXXX); \
	 for mode in pins framing; do \
	  node tests/osh/diff/result_reader_controls.mjs $(abspath $(OSH_DIR)/osh_diff) $(abspath $(OSH_DIR)/osh_toy) tests/osh/diff/fixtures.txt $$d/$$mode $$mode > $$d/$$mode.log; \
	  node tests/osh/diff/result_reader_controls.mjs $(abspath $(OSH_DIR)/osh_diff_asan) $(abspath $(OSH_DIR)/osh_toy) tests/osh/diff/fixtures.txt $$d/$$mode-asan $$mode $$d/$$mode/actual-host.result > $$d/$$mode-asan.log; \
	  tail -1 $$d/$$mode.log; tail -1 $$d/$$mode-asan.log; \
	 done; echo "reader artifacts: $$d"

test-osh-followon-mutants: $(OSH_DIR)/osh $(OSH_DIR)/osh_numeric_priority_asan $(OSH_DIR)/osh_diff $(OSH_DIR)/osh_toy
	sh tests/osh/qualify_followons.sh $(abspath $(OSH_DIR))
