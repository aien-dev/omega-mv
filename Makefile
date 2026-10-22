# omega-host: OSH, Omega's Linux host adapter, built and tested inside the Omega commit pinned by omega.lock.
#
#   make test     check Omega out at omega.lock (build/omega), lay this repo over it, run every OSH test
#   make clean
W ?= build/omega
TESTS = osh-gen-check test-osh-lex test-osh-parse test-osh-expand test-osh-e2e osh-trace test-osh-host test-osh-caps test-osh-journal

.PHONY: test tree clean
tree:
	./tools/omega_tree.sh $(W)

test: tree
	$(MAKE) -C $(W) $(TESTS)
	@echo "OMEGA-HOST: PASS ($(words $(TESTS)) targets)"

clean:
	rm -rf build
