# Optional Linux host diagnostics. No engine linkage or default collection.
.PHONY: test-trace
test-trace:
	@tests/trace/run.sh
