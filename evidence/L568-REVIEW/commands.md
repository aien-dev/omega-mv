# Executed commands and exits

Working directory: `/home/drakestapleton/workspace/omega-osh-batch-corrective-review-20261022`. Candidate7177ad74; outputs under own `build/L568`. `$PWD` below means that directory. Commands used source copies/overrides solely in owned output; no candidate file edits. Public supplemental probes are in this packet. Existing author scripts are pinned in source-pins.json.

```sh
git show 3f9eee71:tests/osh/host/test_osh_host.c > build/L568/old_test_osh_host.c
env --default-signal=INT --ignore-signal=QUIT quiet-build make --no-print-directory OUT_DIR="$PWD/build/L568/red" OSH_HOST_SRCS="$PWD/build/L568/old_test_osh_host.c src/osh/host/osh_req.c src/osh/host/osh_vars.c src/osh/host/osh_builtin.c src/osh/host/osh_exec.c src/osh/host/osh_journal.c src/sha256.c" test-osh-host
# exit2, exactly5 assertions; raw/red-make.log + raw/red-osh-host-host.out

env --default-signal=INT --ignore-signal=QUIT quiet-build make --no-print-directory OUT_DIR="$PWD/build/L568/green" test-osh-host
# exit0; raw/green-make.log and both host.out files

quiet-build make -s -j2 OUT_DIR="$PWD/build/L568/core" test-osh-prompt-signals test-osh-diff test-osh-e2e osh-trace
# exit0; raw/core-make.log and individual outputs

quiet-build node tests/osh/diff/result_reader_controls.mjs build/L568/core/osh/osh_diff build/L568/core/osh/osh_toy tests/osh/diff/fixtures.txt build/L568/pins pins build/L568/core/osh/diff-host.result
# exit0,22 comparator assertions
quiet-build node tests/osh/diff/result_reader_controls.mjs build/L568/core/osh/osh_diff_asan build/L568/core/osh/osh_toy tests/osh/diff/fixtures.txt build/L568/framing framing build/L568/core/osh/diff-host.result
# exit0,17 comparator assertions
quiet-build node tests/osh/e2e/exit_boundary_controls.mjs build/L568/core/osh/osh build/L568/exit-original
# exit0,36 actual Bash comparisons
quiet-build node tests/osh/diff/result_reader_mutants.mjs build/L568/core/osh/diff-host.result pins
quiet-build node tests/osh/diff/result_reader_mutants.mjs build/L568/core/osh/diff-host.result framing
quiet-build node tests/osh/e2e/exit_boundary_mutants.mjs
quiet-build node tests/osh/host/prompt_signal_mutants.mjs
# each exit0;10 compiled controls with exact expected failure counts; raw/*mutants.log and subdirectories

node evidence/L568-REVIEW/entry-controls.mjs
# exit0; child env/quiet-build/Make calls and exits retained in raw/entry-controls

quiet-build cc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -Isrc/osh/host -Isrc -O2 -o build/L568/entry-probe evidence/L568-REVIEW/entry-probe.c src/osh/host/osh_req.c src/osh/host/osh_vars.c src/osh/host/osh_builtin.c src/osh/host/osh_exec.c src/osh/host/osh_journal.c src/sha256.c
build/L568/entry-probe
# both exit0
env --default-signal=INT --ignore-signal=QUIT build/L568/green/osh-host/test_osh_host --helper sigdisp
# exit0, INT dfl QUIT ign
bash -c 'exec 37</dev/null; exec "$1" --helper fdlist' _ "$PWD/build/L568/green/osh-host/test_osh_host"
# exit0, fd37 preserved (one unrelated descriptor)

quiet-build make -s -j2 OUT_DIR="$PWD/build/L568/signal" 'OSH_PROMPT_SRCS=$(filter-out src/osh/host/osh_main.c,$(OSH_BIN_SRCS)) evidence/L568-REVIEW/signal-probe.c' "$PWD/build/L568/signal/osh/test_prompt_signal_restore" "$PWD/build/L568/signal/osh/test_prompt_signal_restore_asan"
build/L568/signal/osh/test_prompt_signal_restore
build/L568/signal/osh/test_prompt_signal_restore_asan
# each exit0;4746 state checks per executable

quiet-build node evidence/L568-REVIEW/adversarial.mjs adversarial-policy-corrected
# exit1;68 assertions,62pass/6 genuine preexisting F2 failures; individual JSON records retain actual argv/env-independent oracle output

git show 3695dd59^:src/osh/host/osh_shell.c > build/L568/pre-exit-shell.c
quiet-build make -s -j2 OUT_DIR="$PWD/build/L568/pre-exit" "OSH_BIN_SRCS=\$(OSH_DIR)/osh_units.c \$(filter-out src/osh/host/osh_shell.c src/osh/host/osh_codes.c,\$(OSH_PROG_HOSTSRCS)) $PWD/build/L568/pre-exit-shell.c src/osh/host/osh_codes.c \$(OSC_LIB) src/runtime/rx_caproot.c" osh
# exit0. raw/preexisting-results.json records15 before/after/Bash invocations; no signal/error/deadline kills

sha256sum -c evidence/L562-HOST-ENTRY/SHA256SUMS
# exit1 (F1). This manifest uses repository-relative paths.
# Other four L553 manifests: (cd PACKET && sha256sum -c SHA256SUMS), all exit0.
git diff --exit-code 3f9eee71 7177ad74 -- src mk Makefile
git diff --exit-code c100a6eb 7177ad74 -- Makefile tools/with_build_lock.sh
# both exit0
node evidence/L568-REVIEW/seal.mjs
# exit0; source/ELF/tool pins, raw collection, complete old-test reconstruction and lossless audit
```

All `.exit` files are the actual recorded exits. Early aborted expanded probe and initial capability-invalid supplemental probes are retained as development evidence and excluded from acceptance; see review explanation. Do not rerun fresh-output scripts into their existing directories. For a new replay choose fresh output names consistently. `seal.mjs` is the packet collector for this actual run, not a standalone gate. Files under raw are historical immutable outputs, not commands to execute.

Metadata finalization regenerates only the root `.crumb` after staging this review packet, then verifies with the pinned tool. The initial root-stale result from adding new evidence is retained in `raw/crumb-initial-stale.log`; this is reviewer-added evidence bookkeeping, independent of candidate source verdicts. Final verifier result is recorded in the handoff. All `raw/` paths in this packet refer to members of the lossless `raw.tar.gz` archive.
