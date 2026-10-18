# osh shell core (aien-architecture#158). Picked up by `-include mk/*.mk`; not part of `all` or `test`.
# src/osh/osh_lex.osc and osh_parse.osc are generated from the .osc.in templates and osh_layout.lst by osh_gen.sh
# (checked in with their output); the drivers tests/osh/test_osh_lex.c and test_osh_parse.c run them in the OSC
# interpreter and as native AArch64.
#   make osh-gen          regenerate src/osh/osh_lex.osc, osh_parse.osc and osh_layout.h
#   make osh-gen-check    regenerating gives byte-identical files
#   make test-osh-lex     check + lexer driver, plain and ASan/UBSan (exact-size buffers); OSH_FUZZ=N (default 100000), OSH_FUZZ_SEED=S
#   make osh-bash-verify  prove tests/osh/vectors/bash_vectors.tsv against real bash 5.2 (needs bash, not part of test-osh-lex)
#   make test-osh-parse   check + parser driver (lexer + parser units, reference tokenizer + parser), plain and ASan/UBSan
#   make test-osh-diff    cross-adapter pipeline differential (host native + interpreter; aienos NOT_RUN without OSH_DIFF_NATIVE)
ifndef OSH_MK
OSH_MK := 1
.PHONY: osh-gen osh-gen-check test-osh-lex test-osh-parse test-osh-expand osh test-osh-e2e osh-bash-verify test-osh-diff
OSH_DIR = $(OUT_DIR)/osh
OSH_FUZZ ?= 100000
OSH_GEN = src/osh/osh_gen.sh
OSH_GEN_ARGS = src/osh/osh_layout.lst src/osh/osh_lex.osc.in src/osh/osh_lex.osc src/osh/osh_layout.h
OSH_GEN_ARGS_P = src/osh/osh_layout.lst src/osh/osh_parse.osc.in src/osh/osh_parse.osc src/osh/osh_layout.h
OSH_GEN_ARGS_X = src/osh/osh_layout.lst src/osh/osh_expand.osc.in src/osh/osh_expand.osc src/osh/osh_layout.h
OSH_SRCS = tests/osh/test_osh_lex.c src/osh/osh_lex_ref.c $(OSC_LIB)
OSH_PSRCS = tests/osh/test_osh_parse.c src/osh/osh_lex_ref.c src/osh/osh_parse_ref.c $(OSC_LIB)
OSH_HDRS = src/osh/osh_layout.h src/osh/osh_lex_ref.h src/osh/osh_parse_ref.h $(OSC_HDRS)
OSH_FLAGS = -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc -Isrc/compiler -Isrc/osh

osh-gen:
	sh $(OSH_GEN) gen $(OSH_GEN_ARGS)
	sh $(OSH_GEN) gen $(OSH_GEN_ARGS_P)
	sh $(OSH_GEN) gen $(OSH_GEN_ARGS_X)

osh-bash-verify:
	bash tests/osh/vectors/bash_verify.sh

osh-gen-check:
	sh $(OSH_GEN) check $(OSH_GEN_ARGS)
	sh $(OSH_GEN) check $(OSH_GEN_ARGS_P)
	sh $(OSH_GEN) check $(OSH_GEN_ARGS_X)

$(OSH_DIR)/test_osh_lex: $(OSH_SRCS) $(OSH_HDRS)
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_FLAGS) -O2 -o $@ $(OSH_SRCS)

$(OSH_DIR)/test_osh_lex_asan: $(OSH_SRCS) $(OSH_HDRS)
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_FLAGS) $(OSC_ASAN) -o $@ $(OSH_SRCS)

$(OSH_DIR)/test_osh_parse: $(OSH_PSRCS) $(OSH_HDRS)
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_FLAGS) -O2 -o $@ $(OSH_PSRCS)

$(OSH_DIR)/test_osh_parse_asan: $(OSH_PSRCS) $(OSH_HDRS)
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_FLAGS) $(OSC_ASAN) -o $@ $(OSH_PSRCS)

test-osh-lex: osh-gen-check $(OSH_DIR)/test_osh_lex $(OSH_DIR)/test_osh_lex_asan
	$(abspath $(OSH_DIR)/test_osh_lex) src/osh/osh_lex.osc tests/osh/vectors $(OSH_FUZZ) > $(OSH_DIR)/lex.out
	@tail -12 $(OSH_DIR)/lex.out; tail -1 $(OSH_DIR)/lex.out | grep -q '^OSH_LEX_PASS$$'
	$(abspath $(OSH_DIR)/test_osh_lex_asan) src/osh/osh_lex.osc tests/osh/vectors $(OSH_FUZZ) > $(OSH_DIR)/lex_asan.out
	@tail -3 $(OSH_DIR)/lex_asan.out; tail -1 $(OSH_DIR)/lex_asan.out | grep -q '^OSH_LEX_PASS$$'
	@echo "test-osh-lex: PASS (lexer unit only; interp + native + C reference + chunked + vectors + fuzz; plain and ASan/UBSan)"

test-osh-parse: osh-gen-check $(OSH_DIR)/test_osh_parse $(OSH_DIR)/test_osh_parse_asan
	$(abspath $(OSH_DIR)/test_osh_parse) src/osh/osh_lex.osc src/osh/osh_parse.osc tests/osh/vectors tests/osh/parse_vectors $(OSH_FUZZ) > $(OSH_DIR)/parse.out
	@tail -14 $(OSH_DIR)/parse.out; tail -1 $(OSH_DIR)/parse.out | grep -q '^OSH_PARSE_PASS$$'
	$(abspath $(OSH_DIR)/test_osh_parse_asan) src/osh/osh_lex.osc src/osh/osh_parse.osc tests/osh/vectors tests/osh/parse_vectors $(OSH_FUZZ) > $(OSH_DIR)/parse_asan.out
	@tail -3 $(OSH_DIR)/parse_asan.out; tail -1 $(OSH_DIR)/parse_asan.out | grep -q '^OSH_PARSE_PASS$$'
	@echo "test-osh-parse: PASS (lexer + parser units; interp + native + C reference lexer and parser + chunked + vectors + fuzz; plain and ASan/UBSan)"

OSH_XSRCS = tests/osh/test_osh_expand.c src/osh/osh_lex_ref.c src/osh/osh_parse_ref.c src/osh/osh_expand_ref.c src/osh/host/osh_core.c src/osh/host/osh_codes.c src/osh/host/osh_req.c $(OSC_LIB)
OSH_XHDRS = $(OSH_HDRS) src/osh/osh_expand_ref.h src/osh/host/osh_core.h src/osh/host/osh_host.h
OSH_XFLAGS = $(OSH_FLAGS) -Isrc/osh/host -Isrc/runtime

$(OSH_DIR)/test_osh_expand: $(OSH_XSRCS) $(OSH_XHDRS)
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_XFLAGS) -O2 -o $@ $(OSH_XSRCS)

$(OSH_DIR)/test_osh_expand_asan: $(OSH_XSRCS) $(OSH_XHDRS)
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_XFLAGS) $(OSC_ASAN) -o $@ $(OSH_XSRCS)

test-osh-expand: osh-gen-check $(OSH_DIR)/test_osh_expand $(OSH_DIR)/test_osh_expand_asan
	$(abspath $(OSH_DIR)/test_osh_expand) src/osh/osh_lex.osc src/osh/osh_parse.osc src/osh/osh_expand.osc tests/osh/vectors $(OSH_FUZZ) > $(OSH_DIR)/expand.out
	@tail -16 $(OSH_DIR)/expand.out; tail -1 $(OSH_DIR)/expand.out | grep -q '^OSH_EXPAND_PASS$$'
	$(abspath $(OSH_DIR)/test_osh_expand_asan) src/osh/osh_lex.osc src/osh/osh_parse.osc src/osh/osh_expand.osc tests/osh/vectors $(OSH_FUZZ) > $(OSH_DIR)/expand_asan.out
	@tail -3 $(OSH_DIR)/expand_asan.out; tail -1 $(OSH_DIR)/expand_asan.out | grep -q '^OSH_EXPAND_PASS$$'
	@echo "test-osh-expand: PASS"

OSH_UNIT_SRCS = src/osh/osh_lex.osc src/osh/osh_parse.osc src/osh/osh_expand.osc
OSH_PROG_HOSTSRCS = $(wildcard src/osh/host/osh_*.c)
OSH_BIN_SRCS = $(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_codes.c,$(OSH_PROG_HOSTSRCS)) src/osh/host/osh_codes.c $(OSC_LIB) src/runtime/rx_caproot.c

# the three unit sources become C arrays (od + sed, no scripting language)
$(OSH_DIR)/osh_units.c: $(OSH_UNIT_SRCS)
	@mkdir -p $(OSH_DIR)
	{ echo "#include <stddef.h>"; \
	  for u in lex parse expand; do \
	    echo "const unsigned char osh_unit_$$u[] = {"; \
	    od -An -v -tu1 src/osh/osh_$$u.osc | sed 's/^ *//; s/  */,/g; s/$$/,/'; \
	    echo "0};"; \
	    echo "const size_t osh_unit_$${u}_len = sizeof osh_unit_$$u - 1;"; \
	  done; } > $@

$(OSH_DIR)/osh: $(OSH_BIN_SRCS) $(OSH_XHDRS) src/osh/host/osh_shell.h src/osh/host/osh_priv.h
	$(CC) $(OSH_XFLAGS) -O2 -o $@ $(OSH_BIN_SRCS)

osh: $(OSH_DIR)/osh

$(OSH_DIR)/argprint: tests/osh/e2e/argprint.c
	@mkdir -p $(OSH_DIR)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -o $@ $<

$(OSH_DIR)/test_osh_e2e: tests/osh/e2e/test_osh_e2e.c
	@mkdir -p $(OSH_DIR)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -o $@ $<

test-osh-e2e: osh-gen-check $(OSH_DIR)/osh $(OSH_DIR)/argprint $(OSH_DIR)/test_osh_e2e
	rm -rf $(abspath $(OSH_DIR))/state; XDG_STATE_HOME=$(abspath $(OSH_DIR))/state $(abspath $(OSH_DIR)/test_osh_e2e) $(abspath $(OSH_DIR)/osh) $(abspath $(OSH_DIR)/argprint) > $(OSH_DIR)/e2e.out
	@tail -3 $(OSH_DIR)/e2e.out; tail -1 $(OSH_DIR)/e2e.out | grep -q '^OSH_E2E_PASS$$'
	@echo "test-osh-e2e: PASS"

# Core-only step trace (OSH-AIENOS-0): the lexer, parser and expander run over fixture scripts through osh_core_call, nothing
# is executed. `make osh-trace` runs every tests/osh/trace/*.sh native AND interpreted, requires the two traces to be
# identical, and requires them to equal the committed .trace file. `make osh-trace-gen` rewrites the .trace files.
# Optional per fixture: tNN.status (fake pipeline statuses) and tNN.args (positionals); see tests/osh/osh_trace.c.
.PHONY: osh-trace osh-trace-gen
OSH_TRSRCS = tests/osh/osh_trace.c src/osh/host/osh_core.c src/osh/host/osh_codes.c $(OSC_LIB)

$(OSH_DIR)/osh_trace: $(OSH_TRSRCS) $(OSH_XHDRS)
	@mkdir -p $(OSH_DIR)
	$(CC) $(OSH_XFLAGS) -O2 -o $@ $(OSH_TRSRCS)

# Each trace must end with "event exit=" (the driver prints it last) and hold at least one other line: an
# empty, cut-off or do-nothing trace never passes and is never written.
# A .trace, .status or .args with no .sh fails. osh-trace-gen writes all traces to a scratch directory first and copies
# only when every script passed.
osh-trace-gen: osh-gen-check $(OSH_DIR)/osh_trace
	@for x in tests/osh/trace/*.trace tests/osh/trace/*.status tests/osh/trace/*.args; do [ -e "$$x" ] || continue; [ -f "$${x%.*}.sh" ] || { echo "$@: $$x has no matching .sh"; exit 1; }; done; \
	g=$(OSH_DIR)/trace-gen; rm -rf $$g; mkdir -p $$g; \
	for f in tests/osh/trace/*.sh; do b=$$(basename $$f .sh); \
	  $(OSH_DIR)/osh_trace interp $(OSH_UNIT_SRCS) $$f > $$g/$$b.interp || exit 1; \
	  $(OSH_DIR)/osh_trace native $(OSH_UNIT_SRCS) $$f > $$g/$$b.native || exit 1; \
	  cmp $$g/$$b.interp $$g/$$b.native || { echo "osh-trace-gen: native and interpreter differ on $$f"; exit 1; }; \
	  { tail -n 1 $$g/$$b.native | grep -q '^event exit=' && grep -v '^event exit=' $$g/$$b.native | grep -q .; } || { echo "osh-trace-gen: empty or cut-off trace for $$f"; exit 1; }; \
	done; \
	for f in tests/osh/trace/*.sh; do b=$$(basename $$f .sh); cp $$g/$$b.native tests/osh/trace/$$b.trace || exit 1; done; \
	echo "osh-trace-gen: wrote traces"

osh-trace: osh-gen-check $(OSH_DIR)/osh_trace
	@for x in tests/osh/trace/*.trace tests/osh/trace/*.status tests/osh/trace/*.args; do [ -e "$$x" ] || continue; [ -f "$${x%.*}.sh" ] || { echo "$@: $$x has no matching .sh"; exit 1; }; done; \
	n=0; for f in tests/osh/trace/*.sh; do b=$$(basename $$f .sh); \
	  $(OSH_DIR)/osh_trace interp $(OSH_UNIT_SRCS) $$f > $(OSH_DIR)/$$b.interp || exit 1; \
	  $(OSH_DIR)/osh_trace native $(OSH_UNIT_SRCS) $$f > $(OSH_DIR)/$$b.native || exit 1; \
	  cmp $(OSH_DIR)/$$b.interp $(OSH_DIR)/$$b.native || { echo "osh-trace: native and interpreter differ on $$f"; exit 1; }; \
	  { tail -n 1 $(OSH_DIR)/$$b.native | grep -q '^event exit=' && grep -v '^event exit=' $(OSH_DIR)/$$b.native | grep -q .; } || { echo "osh-trace: empty or cut-off trace for $$f"; exit 1; }; \
	  cmp $(OSH_DIR)/$$b.native tests/osh/trace/$$b.trace || { echo "osh-trace: $$f differs from the committed trace"; exit 1; }; \
	  n=$$((n+1)); done; echo "osh-trace: PASS ($$n scripts, native == interpreter == committed trace)"

# Cross-adapter pipeline differential (omega#568): tests/osh/diff/fixtures.txt through the real compiled shell core on the
# Linux host adapter, native and interpreted, against the expectations, with injected faults and resource limits; plus the
# aienos column, which is NOT_RUN unless a native result file is given (OSH_DIFF_NATIVE=FILE; a raw QEMU serial log is
# fine). Plain and ASan/UBSan, the comparator mutants (tests/osh/diff/mutants.sh), and a check that the TEST-ONLY observer
# is absent from the shipped osh. Format and toy operations: tests/osh/diff/README.md.
#   make test-osh-diff [OSH_DIFF_NATIVE=FILE] [OSH_DIFF_REQUIRE_NATIVE=1]
.PHONY: test-osh-diff
OSH_DIFF_SRCS = tests/osh/diff/osh_diff.c $(OSH_DIR)/osh_units.c $(filter-out src/osh/host/osh_main.c,$(OSH_PROG_HOSTSRCS)) $(OSC_LIB) src/runtime/rx_caproot.c
OSH_DIFF_HDRS = $(OSH_XHDRS) src/osh/host/osh_shell.h src/osh/host/osh_priv.h
OSH_DIFF_FLAGS = $(OSH_XFLAGS) -DOSH_DIFF_TEST_HOOKS
# identity pins (README "Pins"), recursive so they are only computed when the recipe runs
OSH_DIFF_PIN_OMEGA = $(shell git rev-parse HEAD 2>/dev/null || echo unknown)$(shell test -n "$$(git status --porcelain -- src tests mk 2>/dev/null)" && echo -dirty)
OSH_DIFF_PIN_CC = $(notdir $(CC))-$(shell $(CC) -dumpfullversion 2>/dev/null || $(CC) -dumpversion)@$(shell sha256sum "$$(command -v $(CC))" | cut -c1-16)
OSH_DIFF_PIN_HARNESS = $(shell cat tests/osh/diff/osh_diff.c tests/osh/diff/osh_toy.c tests/osh/diff/mutants.sh | sha256sum | cut -c1-64)
OSH_DIFF_PIN_AIENOS = $(shell head -1 aienos.lock 2>/dev/null || echo unknown)
OSH_DIFF_PINS = --pin omega=$(OSH_DIFF_PIN_OMEGA) --pin cc=$(OSH_DIFF_PIN_CC) --pin harness=$(OSH_DIFF_PIN_HARNESS) --pin aienos=$(OSH_DIFF_PIN_AIENOS)
OSH_DIFF_NATIVE_ARG = $(if $(OSH_DIFF_NATIVE),--native $(abspath $(OSH_DIFF_NATIVE))) $(if $(OSH_DIFF_REQUIRE_NATIVE),--require-native)

$(OSH_DIR)/osh_diff: $(OSH_DIFF_SRCS) $(OSH_DIFF_HDRS)
	$(CC) $(OSH_DIFF_FLAGS) -O2 -o $@ $(OSH_DIFF_SRCS)

$(OSH_DIR)/osh_diff_asan: $(OSH_DIFF_SRCS) $(OSH_DIFF_HDRS)
	$(CC) $(OSH_DIFF_FLAGS) $(OSC_ASAN) -o $@ $(OSH_DIFF_SRCS)

$(OSH_DIR)/osh_toy: tests/osh/diff/osh_toy.c
	@mkdir -p $(OSH_DIR)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -o $@ $<

# The ASan/UBSan run must PASS with exactly two host NOT_RUN: fixture f33 (RLIMIT_AS cannot bind an ASan process), both runs.
test-osh-diff: osh-gen-check $(OSH_DIR)/osh $(OSH_DIR)/osh_diff $(OSH_DIR)/osh_diff_asan $(OSH_DIR)/osh_toy
	@if nm $(OSH_DIR)/osh | grep -q osh_diff_test_hooks_compiled; then echo "FAIL: shipped osh contains the TEST-ONLY diff observer"; exit 1; fi
	@nm $(OSH_DIR)/osh_diff | grep -q osh_diff_test_hooks_compiled || { echo "FAIL: the diff runner lacks the observer"; exit 1; }
	@echo "diff observer: absent from the shipped osh, present in the runner"
	$(abspath $(OSH_DIR)/osh_diff) --strict-host $(OSH_DIFF_PINS) --emit $(abspath $(OSH_DIR))/diff-host.result $(OSH_DIFF_NATIVE_ARG) $(abspath $(OSH_DIR)/osh_toy) tests/osh/diff/fixtures.txt > $(OSH_DIR)/diff.out; rc=$$?; cat $(OSH_DIR)/diff.out; test $$rc -eq 0 && tail -1 $(OSH_DIR)/diff.out | grep -q '^OSH_DIFF_PASS$$'
	$(abspath $(OSH_DIR)/osh_diff_asan) $(OSH_DIFF_PINS) $(OSH_DIFF_NATIVE_ARG) $(abspath $(OSH_DIR)/osh_toy) tests/osh/diff/fixtures.txt > $(OSH_DIR)/diff_asan.out; rc=$$?; tail -3 $(OSH_DIR)/diff_asan.out; test $$rc -eq 0 && tail -1 $(OSH_DIR)/diff_asan.out | grep -q '^OSH_DIFF_PASS$$' && grep -q '^summary: host PASS [0-9]* FAIL 0 NOT_RUN 2 ' $(OSH_DIR)/diff_asan.out && grep -q '^f33_bounded_memory .* NOT_RUN .*RLIMIT_AS cannot apply under ASan' $(OSH_DIR)/diff_asan.out
	sh tests/osh/diff/mutants.sh $(abspath $(OSH_DIR)/osh_diff) $(abspath $(OSH_DIR)/osh_toy) tests/osh/diff/fixtures.txt $(abspath $(OSH_DIR))/diff-host.result $(abspath $(OSH_DIR))/diff-mutants
	@echo "test-osh-diff: PASS (host adapter: native + interpreter units, plain and ASan/UBSan, faults and limits, comparator mutants; aienos column $(if $(OSH_DIFF_NATIVE),compared against $(OSH_DIFF_NATIVE),NOT_RUN: no native result supplied))"
endif
