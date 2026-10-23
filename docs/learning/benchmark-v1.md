# Public benchmark component v1

L775 continues Omega #653 under #652. This is a bounded, host-native component,
not a complete benchmark runner or a new judge. L776 independently owns the
public programs and oracle. Compiler L408, native execution L408/L605, GEN
custody, conformance, trainer L491/parent L487 and the sole Renda publisher keep
their existing responsibilities.

Source starts from Omega main `cd5897e3f92f72a015892c67b17f096e433b54f7`.
The consumed source-only toolchain is omega-bridge
`0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`; the rebuilt compiler SHA256 is
`191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.
The `.om` compiler source SHA256 is
`8608fa11f6a2cf1c50db22d871eef16b00d5bc719bc1a222cc3e456984ac5ebf`.
No mutable sibling compiler is consumed. The prior source-only bootstrap and
fixpoint receipt are preserved in the handoff; resumed execution rechecks both
hashes. New implementation and tests are handwritten `.om`.

## Implemented interface

`core.om` is composed with an owned caller as a single translation unit. The
caller fills bounded arrays; there is no implicit file loading, network access,
receipt-signature validation or process execution. Digest equality checks
verify the supplied identities agree, not that an untrusted caller measured
them. Production receipt authentication and actual source hashing must come
from the existing execution/evidence owner. This component grants no authority.

`LT` contains at most 64 public task records of 32 `ulong` words each:

| Offset | Meaning |
|---|---|
| 0 | Nonzero task ID, unique across the manifest |
| 1 | Split: 1 train, 2 development, 3 adaptive, 4 final |
| 2 | Visibility: 1 public; all other values refused |
| 3 | Nonzero algorithmic family ID |
| 4 | Source profile ID, exactly 1 |
| 5 | Record schema version, exactly 1 |
| 6 | Nonzero independent oracle ID |
| 7 | Nonzero generator/template family ID |
| 8..11 | Statement SHA256 |
| 12..15 | Candidate source SHA256 |
| 16..19 | Normalized structure SHA256 |
| 20..23 | Algorithmic family SHA256 |
| 24..27 | Generator/template lineage SHA256 |
| 28, 29 | Input and output byte bounds, each 1..256 |
| 30 | Declared step bound, 1..1,000,000 |
| 31 | Reserved, must be zero |

Each digest is four consecutive big-endian 64-bit words of the 32 digest bytes;
all-zero identities are refused. `lmanifest(n)` accepts only public train/dev
records. Duplicate IDs, statement hashes and source hashes are refused in any
split. Across splits it also refuses matching numeric family/template IDs or
structure/family/lineage digests. Metadata must come from the independent
manifest owner: matching hashes do not establish semantic equivalence, and
changing a claimed hash cannot validate its source. This builder API refuses
adaptive/final records rather than opening, retiring or relabeling protected
tasks. No sealed campaign was accessed.

Manifest result codes are 0 valid; 1 count; 2 ID/split; 3 schema/profile;
4 absent family/oracle/template; 5 nonpublic partition; 6 zero digest;
7 bounds/reserved; 8 duplicate ID; 9 duplicate statement; 10 duplicate source;
11 cross-split template ID; 12 cross-split structure; 13 cross-split family
digest; 14 cross-split lineage; 15 cross-split numeric family.

`lobserve(status,wantcode,gotcode,wantn,gotn)` compares `LWant` and `LGot`
against independent expected bytes. Statuses 1 syntax, 2 type, 3 unsupported,
4 timeout, 5 resource and 6 operational pass through unchanged. Status 0 means
the executor completed. Completed result codes are 0 correct, 7 wrong bytes,
8 trap mismatch, 9 empty actual output, 10 malformed observation. Only declared
runtime traps 101..107 can be correct nonzero exits, with zero output bytes.
A zero-exit empty result is never a successful output task. A successful compiler
exit alone is never an observation of correct behavior. Code 11 is explicit
abstention, accepted by accounting only.

`LPin[0..3]` is the expected compiler SHA256, `[4..7]` its observation,
`[8..11]` expected executable SHA256 and `[12..15]` its observation. `lpins()`
compares every word and rejects zero identities. Real identity measurement is
outside this component, so this does not authenticate arbitrary input records.

`lbegin(n,maxtries,budget_us)` validates and initializes a fresh run; maxtries
is 1..64 per task with an additional total cap of 64 attempts per run. Each
`lattempt(task_index,ordinal,outcome)` charges the supplied `LC` cost vector
before classifying the attempt. Ordinals must be contiguous starting at one.
`LC[0..7]` contains generation, compilation, judge and other microseconds;
input tokens; output tokens; peak bytes; energy microjoules. `LC[8]` says energy
is known (1) or unknown (0). Unknown energy requires the energy value to be zero
and increments an explicit unknown counter; it must not be reported as measured
zero energy. Missing mandatory costs (all-ones), costs above 10^12, accumulator
overflow, identity mismatch, bad ordinals, invalid outcome or an exceeded budget
invalidate the run. Failed identities/costs cannot add successful tasks.
Peak memory is a maximum; other cost fields are sums.

`LS` exposes active[0], task count[1], maxtries[2], attempts[3], pass@1
numerator[4], budgeted-success numerator[5], invalid code[6], costs[8..15],
unknown-energy count[16], time budget[17], completed-valid[18], abstentions[19].
`lfinish()` refuses missing tasks and preserves an earlier failure. Consumers
may report success metrics only when it returns zero and `LS[18]` is one.
Diagnostic totals from an invalid run are not valid benchmark scores.
Codes 20..31 cover active run, invalid limits, cost slot, cost bound, inactive
run, energy flag, identity, task/outcome, ordinal, time budget, missing tasks and
total attempt cap respectively. The API has no durable resume or persistence.

`lnear(an,bn)` reads two public ASCII prompts of at most 256 bytes from the
halves of `LText`. It implements the existing GEN-1 `genjac` lowercased
`[a-z0-9]+` consecutive-word-triple SET Jaccard rule, strictly above one half.
It returns 0 at/below threshold, 1 above threshold, 2 insufficient words and
3 unsupported input/bounds. It is a separate required manifest-owner check;
digest-only `lmanifest` does not call it. It does not detect all paraphrases,
algorithmic overlap or false family declarations. Existing GEN custody and
normalization owners must supply/qualify those records; no parallel authority
or protected split manager is introduced.

## Supported profile from executed probes

| Feature | Evidence at this compiler |
|---|---|
| `ulong` add/subtract/multiply | Modulo 2^64 boundary probes pass |
| Unsigned division/remainder, bit shifts | Values pass; zero division traps 102; shift width traps 107 |
| Checked signed addition | Overflow traps 101 |
| Fixed byte arrays | Dynamic out-of-bounds traps 103 |
| Checked narrowing | `ulong` 256 to `uchar` traps 105 |
| Functions and composition | Executed public and component probes pass |
| Branches, fixed `for`, bounded `while` | Executed probes pass; exceeded loop bound traps 104 |
| Scalar widths/types | Mixed signed/unsigned expression refused, compiler exit 7 |
| Float declaration | Unsupported by this pin, compiler exit 3 |
| Output effects | Bounded `write` observed; `read` exercised by observation consumer |
| Recursion, assertions/trap 106, arbitrary effects | NOT_RUN in this profile; no acceptance claim |
| Native process isolation, launch/wait, timeout and file custody | Missing qualified owned interface, NOT_RUN |

Local variable/parameter identifiers are at most eight bytes, and the compiler
has a 16-register allocation pool. The abandoned implementation violated both;
the continuation shortens two local names and separates record/pair checks to
fit the existing compiler. The compiler itself is unchanged.

## Qualification and limits

The native component test executes 185 checks. Exact-identity bypass and
public-scope bypass mutants compile to native ELF and fail this same test.
Independent public fixtures are consumed at L776 source
`f1ca34e99747fd03371f708d24529126af79b9bb`: recompiling its literal oracle and
actual observable program gives 128 matching bytes; its saturating arithmetic
mutant exits zero but differs in value, and this component refuses it with
wrong-value code 7. This is bounded host-native testing, not formal proof,
general native translation equivalence or physical GPU evidence.

The native benchmark runner, protected split custody, calibrated abstention,
model/direct-search/retrieval arms, sample-size freeze, learning, persistence
and full-cost comparison are NOT_RUN. The existing compiler lacks a qualified
native process/isolation/file seam for this consumer. Shell commands, `cat`,
`timeout`, `readelf`, `sha256sum` and `/usr/bin/time` are disclosed external
development orchestration, not a shipped native runner. The component does not
replace a production consumer, so no existing code or dependency is deleted.

No model or tokenizer was consumed in these tests; their experiment identities
remain NOT_RUN, rather than invented. Raw compiler failures and runtime failures
are retained under `evidence/LEARNING-652/benchmark/`. Per-probe cost files record
coarse host command wall/user/system seconds and peak RSS where measured. Early
abandoned and repair attempts were not instrumented; their costs are UNKNOWN,
not zero. No energy was measured. There is no benchmark speedup, generalization
or full lifecycle cost qualification from these component tests. #653 and #652
remain open, and #654/#655 and later ordered gates are not accepted by this slice.
