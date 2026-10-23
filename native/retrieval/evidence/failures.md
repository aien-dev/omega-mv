# Preserved failures and coverage limits

No failure in this work is treated as a successful run.

Initial consumer compilation failed at concatenated line1178 with error9: the
test attempted to convert bool to ulong, which this seed refuses. The test now
branches on the boolean. Later envelope validation exceeded the register pool
(line1086,error12); splitting envelope parsing from record iteration fixed it.
The first replay of that split trapped103 on a missing JSON field: boolean `or`
does not short-circuit, so an absent token was indexed. Explicit bound guards
precede token reads now. The unchanged negative case passes, and missing-field,
timeout, shard-failure and truncated-result cases are retained. Those initial
terminal outputs were not captured as files at the time; this is a transcription,
not a claim that complete raw build streams or timings were retained.

The unchanged tokenizer reference had two failed builds. Their complete logs
and timings are retained as tokenizer-build* artifacts. Its four successful
count observations deliberately exit2 because the requested count exceeds the
packet count; each stderr and process cost is retained. The trace replay has
the same expected exit2. No model evaluation occurred.

Early setup also tried the Xerj executable at the archive root instead of its
extracted subdirectory (exit127), requested objects from a different local Git
object store (missing object), and probed absent model routes `/openapi.json`
and `/tokenize` (404). These are setup failures with incomplete raw-cost capture.

This evidence is insufficient for a complete lifecycle-cost or held-out
development-workflow comparison. Those gates remain NOT_RUN, not PASS.
