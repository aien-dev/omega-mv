# Existing verification runner replay

Functional gate source:fe66fb9565d04cc6e785ca2247594adacf896e5b.
The existing aien-test runner was used unchanged; it is an existing Rust tool,
not a newly authored native automation implementation. New test generation,
process supervision, deadline handling and verdict logic are in gate.om.
Existing shell dispatch and external SHA256 utilities remain disclosed tools.

The committed manifest is selected by the real planner. Normal planning exits4
BLOCKED_OPERATOR PIPELINE_CONFIG_CHANGED with no unmapped paths. Read-only
--operator preview exits0 RUNNABLE and selects OMEGA-PREFETCH-NATIVE; it runs
no gates and grants no live approval. All legacy/hardware gates remain selected
as required by the existing policy. No exemptions or skipped tests were added.

Actual host runner command in a private detached candidate atfe66fb95:
quiet-build timeout180 aien-test run OMEGA-PREFETCH-NATIVE --allow-dirty --no-cache --evidence-dir <private-directory>

That isolated candidate adds only the exact committed compiler source and
binary from1dffceae. It does not modify an owner's working tree or claim a
merged compiler. Result:PASS, childexit0, observationprefetch_cases53. The
receipt explicitly records tree_clean_before=false and tree_clean_after=false,
because this is a combined development candidate, not clean release evidence.
Cache was disabled. No signed Spark bundle or merge readiness is claimed.

The same exact gate on the clean main-based branch without the compiler:
quiet-build timeout60 aien-test run OMEGA-PREFETCH-NATIVE --no-cache --evidence-dir <another-private-directory>

Result:FAIL rc_nonzero, childexit3. This is the expected missing-prerequisite
refusal, preserved in missing-compiler.json. It is not counted as a passing
repository gate. The dependency must be integrated before a clean run can pass.

Both receipts came from the real existing runner. No GPU resource was taken;
their pool ishost and gpu_held_ms isnull. Timing fields are incidental shared-
host observations, not inference-speed evidence. Full issue640 acceptance and
all other required judges remain pending.
