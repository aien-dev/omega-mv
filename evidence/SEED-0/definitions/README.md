# L408 function export qualification for L779

Date: 2026-10-23. Status: implemented and locally qualified for the bounded
function-export contract. Independent review and publication are pending with
the existing owners. This is a continuation of L408, not completion of all R6,
a replacement compiler, or a claim about merged main.

## Reconciliation and pins

The source base is `seed/omega-in-omega` at
`57f5f4258cebf3d5b2c22853cd062e351265c525`, checked against GitHub. Work is
isolated on `seed/definition-export-L408` in `workspace/omega-definitions-L408`.
The existing brief, ledger, compiler feature requests, issues 599/621/648,
repository Crumb, architecture authority, and current compiler were inspected.
The typed-interface/source-aware owner at
`cd2c41f3c697524629585b2a228e2f0ab5b99c45` supplies the reused native SHA-256.
No prior live compiler worker was found; the existing sleeper was preserved.

The original `workspace/omega-in-omega` dirty R6 IR work is untouched. Its source
SHA-256 remains `04fc0d5f6dcfba93bcd9adecaf63bbc7c883adb05d7e48a7401af3fb9d9b4ecd`
and its unqualified output SHA-256 remains
`c1803273dfdb0b69a2154e3afdebf253b49644414483426d99782b4c701b2bee`.
These are preservation receipts, not trusted build inputs.

| Pinned item | SHA-256 |
| --- | --- |
| Candidate `seed/omc/omc.om` | `04a088ba257eb2a1838d98cb039f5cefcc4a434c6fababdb00e175a8c2603ef6` |
| Candidate `seed/out/omc` | `bcf3abc7d322b1f4e53f65559fb7e10b089a8febb5f82242f919f3aeaf9d54be` |
| Contract `seed/omc/DEFINITIONS.md` | `b0c4acc8a4dabf0b5e333f8f54ad8d7cd80975fea270b6e66a2e88ade1c8b2dc` |
| Existing parent `seed/rung4/om4.bin` | `d0da40e45f1fcc45d9a39c9c52c9d745b545e076970729e51c12d76ef519efab` |
| Reused native SHA-256 source | `12d492b5f99b47b1e525067e1660c7cedbf6b414032cfc8b7e174919a28c052c` |

The final gate6 stage1, stage2 and stage3 executables all match the candidate
hash. Earlier `build-stage*.err/exit` record the successful initial build before
the structured-error partial-write loop was tightened. Final results use the
gate6 candidate above. `pins.sha256` binds source, schema, native tests and the
two corpus exchanges. The manifest consumer must pin all three candidate,
contract and source-corpus identities; the response's schema number alone is
insufficient.

## Executed checks

The inherited `sh seed/omc/gate6.sh` passes its compiler fixed point and all
26 cases. Raw output, errors and exit status are in `inherited-gate6.*`.
No existing test scripts were changed. All new executable source, test input
generators, result assertions and mutation generators are `.om` files.

Native `request.om` emits modes 0 through 12. Each raw request, response,
standard error and process exit is retained as `caseN.*`. Native `check.om`
reads their responses in numerical order and reports `947 0`: 947 assertions,
zero failures, exit 0. Its golden hashes were independently calculated with
the external system `sha256sum` over exact source bytes and separately assembled
identity preimages. The fixed-literal span oracle does not use candidate spans.

| Mode | Condition | Expected compiler exit |
| --- | --- | --- |
| 0 | Baseline with fake function words in comment and text | 0 |
| 1 | Unrelated function inserted ahead of existing functions | 0 |
| 2 | Existing function body changed, identity preserved | 0 |
| 3 | Existing function deleted, absent from output | 0 |
| 4 | Same names under another path, different identities | 0 |
| 5 | Same names under another repository, different identities | 0 |
| 6 | Revision changed, identities preserved | 0 |
| 7 | Duplicate function in one source | 10 |
| 8 | Incorrect return type | 7 |
| 9 | Missing closing token | 15 |
| 10 | Framing version skew | 80 |
| 11 | Declared source length 65,537 | 81 |
| 12 | Truncated source frame | 80 |

Exact digest, count, name, kind, start/end boundaries and complete response
length are asserted. Refusals contain no records. Case 6 demonstrates stable
identity across revisions; rejecting stale revision evidence belongs to the
outer verifier, not the compiler identity generator.

Native `mutate.om` makes exactly one checked replacement in the candidate
source. Each mutant compiles and processes the same cases. The unchanged native
oracle rejects all three:

| Native compiler control | Assertions / failures | Checker exit |
| --- | --- | --- |
| Repository bytes replaced by zeros during identity hashing | 947 / 446 | 1 |
| Closing span shortened by one byte | 947 / 14 | 1 |
| Source digest replaced by zeros in header | 947 / 223 | 1 |

Exact mutant `.om` sources, build results, each exchange and check results are
retained as `control-*`. These controls qualify identity and integrity checks,
not retrieval authorization. They must never be installed as trusted compilers.

The native `corpus.om` packer exports the unchanged public `t1.om` and `t2.om`
from the pinned source base under repository identity `aien-dev/omega-mv`.
`corpus-t1.*` and `corpus-t2.*` retain those raw exchanges. Their same-named
`main` functions have spans [27,86) and [29,108) and different identities.
The compiler parses their struct syntax but only emits function records.

## Raw failures, costs, and limits

No failed attempt was dropped. `request-r0-build.*` records compiler error 16
for a reserved helper name. `check-r0-build.*` and `check-r1-build.*` record
error 12 for register pressure in the initial test oracle. Helpers were renamed
or split, then the final native source compiled and passed. These were test
implementation failures, not successful qualification runs.

`corpus-t1.cost` records one `/usr/bin/time` sample including elapsed time,
CPU time and peak resident memory; its timed response matches the ordinary
response. Timer resolution is too coarse to infer latency or throughput from
this single sample. Total engineering time, tool/API cost and aggregate build
resources were not metered here and are explicitly unknown. No model inference,
GPU workload, service startup or network retrieval was performed in this lane.

Not run: full historical gate5/C-oracle campaign, performance qualification,
the exact maximum 512-function response, exhaustive malformed input fuzzing,
transport credential enforcement, tokenizer packing, Xerj integration, held-out
development tasks, or independent review. Those are not implied by the local
compiler checks. File access, process spawning and native sockets remain
separate compiler facilities/owner dependencies; no foreign runtime shim was
added to bypass them. The existing global compiler priority and TICK-OFF state
were not changed.

To replay, compile the four native test sources with the pinned candidate;
feed the indicated mode byte to `request` and then each resulting request to
the candidate. Concatenate `case{0..12}.response` in numeric order into `check`.
For each mutation, feed its mode byte plus the exact compiler source to
`mutate`, compile that `.om` output, and replay the same requests and checker.
This describes ordinary tool invocation; no new shell, Python, Rust or C test
implementation is part of this slice.
