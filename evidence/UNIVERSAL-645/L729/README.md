# L729: bounded native `.om` workload qualification

Final functional source: `6de092948a7c8f87dd2cd18a0886c335bd635a3e`.
Parent: architecture #295. Issues #625/#621/#645/#623/#624; native seam
dependencies #646/#647. This is a reviewed prerequisite slice, not completion of
any of those issues. Renda retains PR, judge and publication ownership.

## Reproduction and observed result

In a new detached checkout of that source, with no build directory:

```sh
bash tests/universal/recursion/replay.sh
```

Exit0. The existing source-aware builder reconstructed the seed compiler from
the committed source dependency. Final source/compiler/binary identities, all63
gate command invocations and their exits, raw streams and mutations are in
`author-final.json` and `author-final.tar.gz`. Native execution is physical
Linux AArch64 host CPU execution on Spark. GPU execution is NOT_RUN.

- 3464 native/reference observations, zero mismatches, including tree/DAG,
  recursive/iterative accumulation, zero/exact/exceeded resources, arithmetic
  wrap, missing base case, argument order and bounded mutual-call inventory.
- Seven compiled faulty variants caught: omitted fuel charge (behavioral RED),
  frame/storage off-by-one, wrong return, argument order, continuation and child
  order. Mutants executed with real observation mismatches.
- Truncated input exit65; failed request followed by valid requests recovers.
  Session8191 and8192 exit0;8193 or8192 plus one extra byte exit67 without an
  extra response. These are additional transport checks, outside the3464 count.
- Tutorial prints `2016` then `80`, exit0. See
  [the `.om` lesson](../../../tests/universal/recursion/README.md).
- Inherited v1 codec regression at `a071e8d5`: exit0,10000 unique valid and100000
  malformed units,131000 consumed-input-checked requests,9 compiled mutants and
  unchanged legacy compatibility controls. Raw evidence in `inherited-v1.tar.gz`.
  Final6de changes only the new test oracle; existing v1 source bytes are identical.

## Independent review and retained failures

`independent-review.md` records a separate checkout and actual final-source replay:
3464 observations, seven faulty variants and tutorial all pass. Parent verified
reviewer source hashes and native binary against the final committed replay.
Identity agreement is not a translation proof.

Two medium findings were corrected: exact8192-request sessions incorrectly
returned LIMIT, and the independent oracle overwrote version/budget errors with
a later graph error. Original reviewer probes and corrected replay remain in
`independent-review.tar.gz`. No important finding remains within the narrow scope.

`earlier-failures.tar.gz` retains unsupported `break` syntax, register-pool pressure,
sandbox EPERM (no execution), and the actual native exit105 caused by an unmasked
test-output narrowing conversion. The corrected transport masks each byte.
Those attempts are not PASS. No historical receipt was modified.

## Resources and measured comparisons

Fuel counts admitted function entries, including base/null entries. These fixed
bodies do bounded work between entries; this is not an arbitrary-instruction fuel
model. Native calls work in the pinned compiler; another recursion runtime is
unnecessary for these fixtures. Logical frames and storage are contractual,
distinct from native physical storage. Maximum tested logical bounds:4096 entries,
64 live frames,2048 logical bytes; input nodes64.

Pinned compiler `alloc` admits at most16 register slots; `save` uses at most144
bytes per call. Fixture functions allocate no local arrays. A conservative
source-derived bound of70 simultaneous save areas is10080 stack bytes, below
the generated8MiB stack reservation. Independent review checked this argument.
The executable reserves an additional4816 bytes of global pool storage. Actual
native stack high-water and physical RSS high-water are **UNMEASURED**. The
source argument is not machine-code translation validation.

Five alternating paired runs,1024 requests per measurement, final cold replay:

| Workload | Recursive median ms | Iterative median ms | Scope |
|---|---:|---:|---|
| Accumulate63 | 8.724328 | 8.127827 | Recursion loses this comparison |
| Empty accumulation | 5.660655 | 6.256804 | Startup/transport dominate |
| Shared branching tree | 15.585872 | 42.870258 | Explicit-frame baseline costs more here |

These include process creation, input validation and pipe I/O on a shared host.
Samples vary substantially; no quiet-window or isolated-kernel speedup is claimed.
All timed responses were compared before measurements were retained. Native
compilation took3.187931ms and tutorial compilation4.343060ms in this replay;
those are shared-host observations, not stable preparation-cost estimates.
All raw timings, earlier no-win samples and reviewer observations remain archived.

## Concrete remaining dependencies

| Issue | Delivered here | Required next owner/artifact |
|---|---|---|
| #625 | Explicit proposed call/fuel/frame/storage/exhaustion contract | L569/programme owner agreement and accepted versioned profile |
| #621 | v1 preserved; v2 domain/migration requirements specified | Existing codec owner: canonical function/CFG/call/continuation records, malformed/version/identity gates; no v2 codec implemented here |
| #645 | Handwritten recursive tree/tail workloads and iterative controls through existing seeded native compiler | Consume approved v2 upper records and owned lowering; general recursive interchange is NOT_RUN |
| #623 | Independent bounded observation comparisons and compiled faulty controls | Native migration owner: source/upper/lowering/native relation plus authenticated versioned receipt binding; translation proof NOT_RUN |
| #646 | Cache identity/purity/logical-fuel requirements specified | L418/L495 migration owner must supply immutable native semantic-cache API, pending/completed states and invalidation seam; integration/performance NOT_RUN |
| #647 | Full-scan legacy behavior inspected, adoption rule proposed | L418/native verifier owner must supply native transfer/reachability/fact/diagnostic seam; worklist implementation/parity/performance NOT_RUN |
| #624 | Reproducible `.om` tutorial, raw references/mutants/resources/timing evidence | Integrate accepted source-aware/translation/cache/worklist cells and owner-controlled README; whole support matrix incomplete |

Inspected candidates include main `a8ffd2b9`, reviewed621 `cd2c41f3`, reviewed622
`50f75da2`, evidence-only successor `f5b5b3f6`, L418 `seed/c2om` with uncommitted
translator work, L408 `seed/omega-in-omega` with uncommitted compiler work, and
L496 retirement integration `12251e34`. No native semantic-cache/ownership-analysis
API was found in those inspected candidates. This is a scoped absence finding,
not proof no unpublished implementation exists. Ownership requests were recorded
in L418/L569/L608/L707; no response/transfer is inferred from elapsed time.

The legacy semantic cache holds a mutex during callbacks; recursive reentry is
unsafe. Completed-result dependencies remain acyclic. The legacy verifier uses
quiet OR-join propagation then ordered diagnostics, with terminal traps stopping
propagation. Both remain reference-only. No legacy source was optimized and no
foreign fallback, second cache or general verifier was introduced.

Full signed Spark judge, final PR/merge/publication and downstream acceptance are
NOT_RUN. Owner coordination and Renda intake are recorded separately in the local
handoff; this archive does not grant execution or publication authority.
