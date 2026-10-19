# Host memory fragmentation tools (omega#528): tools/memfrag_scan.c (which pages pin each 2 MiB
# pageblock, read from /proc/kpageflags + a process's driver mappings) and tools/memfrag_sample.sh
# (buddyinfo / pagetypeinfo / meminfo / vmstat / GPU mapping sampler). Host only: fixtures, no
# /proc reads, no root, no device. Two mutants must fail the scanner test; the sampler runs once
# against a fixture /proc tree and its line is checked field by field.
.PHONY: test-memfrag
build/memfrag_scan: tools/memfrag_scan.c
	@mkdir -p build
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o $@ tools/memfrag_scan.c
build/test_memfrag_scan: tests/test_memfrag_scan.c tools/memfrag_scan.c
	@mkdir -p build
	gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o $@ tests/test_memfrag_scan.c
test-memfrag: build/memfrag_scan build/test_memfrag_scan
	./build/test_memfrag_scan
	@for m in NO_GPU_TAG NO_FREE_TAIL; do \
		gcc -std=gnu11 -O2 -Wall -Wextra -Werror -DMEMFRAG_MUTANT_$$m -o build/test_memfrag_scan_$$m tests/test_memfrag_scan.c || exit 1; \
		if ./build/test_memfrag_scan_$$m >build/test_memfrag_scan_$$m.log 2>&1; then echo "MUTANT SURVIVED: $$m"; exit 1; fi; \
		echo "$$m mutant: killed ($$(grep -c '^FAIL:' build/test_memfrag_scan_$$m.log) failing checks)"; \
	done
	tests/memfrag/sample_fixture.sh
	@echo "OMEGA_MEMFRAG_GATE PASS"
