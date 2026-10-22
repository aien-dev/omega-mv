# omega-host: OSH, Omega's Linux host adapter, built and tested inside the Omega commit pinned by omega.lock.
#
#   make test     check Omega out at omega.lock (build/omega), lay this repo over it, run every OSH test
#   make clean
W ?= build/omega
.DEFAULT_GOAL := tree
TESTS = osh-gen-check test-osh-lex test-osh-parse test-osh-expand test-osh-e2e osh-trace test-osh-host test-osh-caps test-osh-journal test-osh-diff test-osh-prompt-signals test-osh-prompt-interrupt test-osh-prompt-pty test-osh-exit-numeric-priority test-osh-exit-arguments test-osh-reader-boundaries
QUALIFY_TESTS = test-osh-followon-mutants

.PHONY: test tree clean test-plan qualify
test-plan:
	python3 tools/osh_test_plan.py
	python3 tools/osh_test_plan_controls.py

tree: test-plan
	./tools/omega_tree.sh $(W)

test: tree
	$(MAKE) -C $(W) $(TESTS)
	@echo "OMEGA-HOST: PASS ($(words $(TESTS)) targets)"

qualify: test
	$(MAKE) -C $(W) $(QUALIFY_TESTS)
	@echo "OMEGA-HOST: QUALIFICATION PASS"

clean:
	rm -rf build
