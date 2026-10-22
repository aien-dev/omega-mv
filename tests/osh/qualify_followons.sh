#!/bin/sh
# Run from the private pinned Omega+module overlay, never an owner's checkout.
set -eu
bin=${1:?absolute OSH binary directory required}
case "$bin" in /*) ;; *) echo 'absolute binary directory required' >&2; exit 2;; esac
out=$(mktemp -d "$bin/followon-controls.XXXXXX")
echo "follow-on controls: $out"
node tests/osh/diff/result_reader_controls.mjs "$bin/osh_diff" "$bin/osh_toy" tests/osh/diff/fixtures.txt "$out/reader" pins > "$out/reader.log"
node tests/osh/diff/result_reader_mutants.mjs "$out/reader/actual-host.result" pins > "$out/reader-pins-mutants.log"
node tests/osh/diff/result_reader_mutants.mjs "$out/reader/actual-host.result" framing > "$out/reader-framing-mutants.log"
node tests/osh/e2e/exit_boundary_controls.mjs "$bin/osh" "$out/exit-boundaries" > "$out/exit-boundaries.log"
node tests/osh/e2e/exit_boundary_mutants.mjs > "$out/exit-boundary-mutants.log"
node tests/osh/host/prompt_signal_mutants.mjs > "$out/prompt-signal-mutants.log"
node tests/osh/host/prompt_interrupt_mutants.mjs "$out/prompt-interrupt" > "$out/prompt-interrupt.log"
node tests/osh/e2e/exit_numeric_priority_mutants.mjs "$out/numeric-priority" > "$out/numeric-priority.log"
node tests/osh/e2e/exit_argument_completion_mutants.mjs "$out/exit-arguments" > "$out/exit-arguments.log"
python3 tests/osh/host/test_entry_controls.py "$PWD" "$out/entry" tests/osh/host/fixtures/original-entry-failures.out > "$out/entry.log"
for f in "$out"/*.log; do tail -1 "$f"; done
echo 'OSH_FOLLOWON_MUTANTS_PASS'
