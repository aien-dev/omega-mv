# L776 public oracle replay

Source `f1ca34e99747fd03371f708d24529126af79b9bb` adds public fixtures only.
The repository's `tests/learning/public/README.md` describes the frozen scope.
The receipt records exact dependency identities and evidence limits; raw
stdout/stderr, command text, exits, process costs and hashes are in `replay/`.
Native binaries remain reproducible outputs under `build/learning652/replay/`;
their hashes are recorded without adding executables to the delivery tree.

The original four self-check programs pass. The two new output programs return
the predeclared sixteen values as exactly 128 bytes. A separately compiled
saturating-add program exits zero with an incorrect value; independent
comparison rejects it at byte 33. The existing literal-overflow compiler refusal
and historical checked-conversion trap remain visible.

The new native output fixtures are locally tested. Independent source review,
the full #653 benchmark and #654 curriculum remain pending. No measurement is
claimed as a speed result. GNU time rounds these process durations to 0.00;
that is below its reporting resolution, not zero cost. Earlier elapsed/memory
costs, current unmetered orchestration and energy prevent a complete lifecycle
cost claim. The earlier initial-failure log is a narrative retained from the
abandoned run, not a substitute for missing raw stderr.

Replay uses the recorded existing host commands in `replay/commands.json`.
After those commands, `jq -r .expected_hex tests/learning/public/vectors.json`
fed to the existing `xxd -r -p` tool yields `replay/expected.bin`. Existing `cmp`
checks compare that file to `oracle.run.stdout`, the oracle to
`observable.run.stdout`, and the oracle to `negative-observable-saturating.run.stdout`.
Expected exits are 0, 0, and 1 respectively. Source and artifact identity
checks are recorded in the three SHA-256 manifests. These are development
invocations, not a newly delivered non-Om test runner.
