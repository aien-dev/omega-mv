# Parent startup-reservation review and correction

Base: 2dee6673dc6df329ee4b4913069affd1ea307e29 (L516).

Independent review found synchronous Archive reservation errors were caught without recording a producer-visible error. Before the worker error callback, submit returned true with submitted=1/pending=1 while the unchanged archive read PASS. Injected one fsync EIO against a synthetic clean archive reproduced this in red.log. This is not an observed production data-loss incident.

Fix: record the reservation error synchronously. Existing submit refusal handles it; no event is accepted. Regression green.log shows accepted=false, submitted=0, pending=0, durable=false. The preexisting archive can correctly remain PASS because no new event was accepted. Worker startup failure still reports asynchronously as well.

Original missing-parent test expected acceptance despite synchronously known failure. Its expectation is now explicit synchronous refusal; real asynchronous persistence coverage is retained and strengthened using successful reservation, an oversized frame, and a second queued event after the writer fails. The original mutation runner initially reported false-persistence survived because its test-name selector no longer matched; that failure is preserved. Restored selector and actual async test now kill the mutation by assertion, not by test selection/import failure.

Final executed checks:
- OMEGA_TRACE_BUILD_DIR=$HOME/workspace/targets/L516-parent/build bash tests/trace/run.sh: final-host.log, 51/51, zero skips; native oracle 12 assertions, kernel-denied offline workflow 8 checks.
- Original mutants: final-original-mutants.log, 17/17 assertion-killed.
- Follow-on mutants: mutants.log, 9/9 assertion-killed (same production source, before final async test expansion).
- strace -f -qq -e trace=network with network-deny and review-offline.mjs: offline.log,14 checks; network.log empty.

Prior red/intermediate failures are retained, not passing evidence. Only synthetic host fixtures were exercised. No production archive, model, GPU, or live service changed. Full repository/hardware/native callback/real training qualification NOT_RUN. This correction needs independent desk review; parent authored it and cannot count this report as independent approval. Original L516 head alone remains HOLD for startup refusal. Depend on the complete L516 chain when integrating; no issue closure claim.
