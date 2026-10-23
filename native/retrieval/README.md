# L779 first Om-Xerj qualification slice

This candidate exports compiler definitions, constructs a bounded read query,
verifies real Xerj responses against a frozen public Om corpus, and packs the
verified records using exact counts from the selected model's pinned tokenizer.
All new executable implementation and tests are `.om`.

**This is a component qualification candidate. Native network/credential-provider
attachment, agent admission, held-out development-task comparison, complete
lifecycle costs, and independent review are not complete.** No agent or shared
search service has been switched to this code. No performance benefit is claimed.

## Reconciliation and ownership

The implementation starts from GitHub `aien-dev/omega-mv` main
`cd5897e3f92f72a015892c67b17f096e433b54f7`, on the isolated branch
`qualify/om-xerj-first`. Main had no seeded native implementation at that pin.
The native compiler and protocol work below are proposed branch work, not merged
main behavior. Issue comments and the existing owner handoffs were checked first.

| Existing responsibility | Reused source or dependency |
| --- | --- |
| L408 seeded compiler | `seed/definition-export-L408`, source commit `43f86be20f7257e02622f10a19abd2da2c98a290`; no edits to the original dirty R6 work |
| Typed interface, issue 648 | Semantic versioned data, tagged errors, bounded buffers, authenticated context separate from claimed fields; generated binding/registry attachment remains pending |
| Source-aware codec, issue 621 | Native SHA256 from `cd2c41f3c697524629585b2a228e2f0ab5b99c45`; unchanged vendored source, original license retained |
| L690 transport | Strict bounded JSON from `30762489ee1fc4e0ceed02310bb6f19b0d128b00`; unchanged vendor file; live HTTP/auth provider is still absent |
| L707/L715/L745 adapter owners | This module is a consumer; it does not create a second registry or claim native invocation admission |
| L775/L776 evidence owners | Existing task/cost/admission work continues in its original branches; development-task benchmarking is gated on its authenticated native runner |
| Independent review and publication | Existing review route and L773 Merge Desk retain acceptance/publication; this candidate requests review only |

The root, workspace, repository Crumb, architecture authority, relevant source-aware
and transport handoffs, and issues 621/624/631/648 were inspected. Issues 648 and
631 had no comments at reconciliation. Global coordinator TICK-OFF is unchanged.

The subsequently supplied shared-search workflow was read and applied. Registered
`xerj_map` and `xerj_search` were called through the existing MCP stdio executable.
The live map contained only `setup-docs`, 23 records. It did not cover Om code,
tokenizer code, or prior project decisions. Exact-file/GitHub fallback was used.
The shared service and indexes were not changed. Its unauthenticated loopback
configuration is not the authorization test described below.

## Frozen inputs

| Input | Pin |
| --- | --- |
| Corpus repository | `aien-dev/omega-mv` |
| Corpus revision | `57f5f4258cebf3d5b2c22853cd062e351265c525` |
| Corpus scope | Only `seed/omc/tests/struct/t1.om` and `t2.om`, function definitions only |
| Corpus index | Exact name `om-l779-seed57`, no wildcard or caller-selected index |
| Export compiler executable SHA256 | `bcf3abc7d322b1f4e53f65559fb7e10b089a8febb5f82242f919f3aeaf9d54be` |
| Export compiler source SHA256 | `04a088ba257eb2a1838d98cb039f5cefcc4a434c6fababdb00e175a8c2603ef6` |
| Export contract SHA256 | `b0c4acc8a4dabf0b5e333f8f54ad8d7cd80975fea270b6e66a2e88ade1c8b2dc` |
| Existing compiler used to build the consumer | `omega-bridge` source `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`, executable SHA256 `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9` |
| Xerj release | `v1.0.0-rc.98`, tag source `28de5cfcb0ab5a2078ae001819fd21d8cf132f2e` |
| Xerj aarch64 musl archive SHA256 | `fcf07ca828c8ea2dd6d17e0819649dc6a4580d6f27ba29554c972328f9c6ee9e` |
| Xerj executable SHA256 | `2ca1d9c1a6e01b870040051e1b12d27b59a9edd26fbade8f8f60865a9654af82` |
| Selected model | Existing `qwen3.6-35b-a3b-fp8`; this slice pins its tokenizer, not a new model-serving qualification |
| Tokenizer JSON SHA256 | `5f9e4d4901a92b997e463c1f46055088b6cca5ca61a6522d1b9f64c4bb81cb42` |
| Tokenizer config SHA256 | `5186f0defcd7f232382c7f0aebcd2252d073bb921ab240e407b7ae8745d2b29b` |

Compiler output schema `OMDEFR01` records kind, name, stable symbol identity,
complete source digest, and exact half-open byte span. Stable identity hashes
`OmDefId1` plus length-carried repository/path/kind/name. Revision is excluded
from stable identity and checked separately by retrieval. Leading indentation
and the final newline are outside the function span.

The two files both define `main`; their identities differ. `pins.om` embeds the
exact frozen public source and compiler-derived records. `tests/export.om`
independently compares both complete raw compiler responses with those pins.
Runtime pin checking hashes both full sources and reconstructs both symbol IDs.
This small manually frozen corpus is not an automatic arbitrary-repository indexer.

## Read and verification boundary

`request.om` accepts 1..256 printable ASCII query bytes. It serializes a fixed
read search with size 8 and the exact revision filter. Quotes and backslashes
are escaped. The transport owner must bind it only to
`POST /om-l779-seed57/_search`; the query carries no URL, arbitrary DSL, credential,
method, or index selector. Offline `ingest.om` emits administrative NDJSON and
is not part of the agent read capability.

The response cap is 65,536 bytes, the inherited JSON cap is 4,096 tokens/depth 32,
and there may be at most eight hits. The corpus admits only the two frozen
records. The verifier checks index, schema, repository, revision, path, digest,
identity, name, kind, exact spans, and every returned text byte. It rejects
duplicate identities while allowing identical names in different files.
Any rejected hit clears the entire accepted set. Neither a partial search,
failed shard, timeout, approximate total, nor truncated hit set becomes no-match.

The integer `auth` argument is an in-process precondition supplied by the future
trusted adapter. It is not a credential verifier, wire field, or security boundary
against arbitrary native callers. The offline replay supplies test authority.
Actual credential enforcement was separately exercised against the pinned Xerj
server. Native credential acquisition, revocation, deadline/cancellation and
authenticated-session attachment remain explicit owner dependencies.

| Result code | Meaning |
| --- | --- |
| 0 / 1 | Fully verified / explicit no-match |
| 2 / 3 / 4 | Schema or malformed record / stale revision / wrong scope |
| 5 / 6 / 7 / 8 | Identity / digest / span / exact text mismatch |
| 9 / 10 / 11 / 12 | Limit exceeded / upstream failure or incomplete search / authority precondition / trusted pin failure |
| 13 / 14 / 15 | Evidence budget too small / tokenizer or packet receipt mismatch / verification not ready |

`qualify.om` retains a bounded structured upstream error and reported status in
diagnostics, separately from source evidence. These fields remain untrusted
provider diagnostics. It emits verified citations and withholds text. The
offline `tests/replay.om` exercises verification followed by exact packing;
it is not an installed agent entry point.

## Exact finite token packing

For this corpus there are exactly four nonempty ordered subsets. `packet.om`
serializes each complete packet, including repository/revision/path/spans,
digests, symbol IDs and the `instruction_authority:none` label. `pack.om` checks
the SHA256 of that entire serialization before using its frozen tokenizer count.
It refuses a changed tokenizer pin or packet. It keeps retrieval order and,
when necessary, drops the second whole record; it never cuts source or citations.
The returned token count and dropped-record count are separate typed metadata.
The caller must reserve its own chat/template/system/user tokens separately.

| Packet | Bytes | Exact model tokens |
| --- | ---: | ---: |
| t1 | 477 | 255 |
| t2 | 499 | 263 |
| t1 then t2 | 907 | 502 |
| t2 then t1 | 907 | 502 |

The oracle is unchanged existing `qwen38_prefill` from Sovereign Core
`7bfe36539869d29be4acf8ceb18849e2e1609766`, using HF tokenizers 0.21.4,
`add_special_tokens=true`, no chat rendering. Its verified early-error path
counts one packet of at most 2,000 bytes and exits 2 before output creation or
model loading. The executable SHA256 is
`026072bc2dd884d5e579d4b3936bcc206707a63d0b87f2c14102060a2b49116f`.
The requested impossible count is 2147483647; raw stderr reports the actual count.
The expected exit 2 is explicitly retained, not reported as a normal successful
model run. One syscall trace confirms no model-weight/device/network open.

The unchanged oracle initially failed to build in its CPU-stub configuration
because an existing import was excluded. The next build used a wrongly named
archive and failed to link. The final build linked the unchanged pinned existing
archive `110295ef23dc7561eea371636347c005835f37b82c22f92401aa8a887a3ca990`.
No Rust/C source was created or modified. That archive's development-override
identity is not GPU qualification. Counts were taken only on the pre-model path.

This is an exhaustive finite profile, not a native general Qwen tokenizer.
Changing corpus, packet format, tokenizer or model requires new authoritative
receipts and independent review. Runtime tokenizer custody and model admission
remain with the existing model/adapter owners.

## Evidence and remaining gates

Native checks cover stale revisions, removed/unknown identities, real duplicate
names, swapping the two valid IDs, wrong corpus/repository/path, corrupt hashes,
schema skew, wrong spans/text, duplicate response keys, oversized responses and
hit lists, incomplete/failed searches, explicit no-match and structured errors.
Compiled identity/authority/scope bypass controls fail the unchanged checks.
The compiler owner additionally ran 947 assertions, 26 inherited compiler cases,
three identical build generations and three independent defective-exporter
controls; its receipt remains on the original owner branch.

The isolated external Xerj process used loopback ports 19280..19282, auth enabled,
lexical embeddings and no external reranking/feedback. A fresh administrative
secret stayed in process memory via a config pipe. A minted key had only `read`
on the exact corpus index. Its raw secret was neither saved nor printed.
The reader returned 200 on its corpus, 403 on another existing index and on
writes, and missing credentials returned 401. The admin positive controls
returned 201/200 on that other index. No-match returned 200 with zero hits.
The process was stopped after qualification. Raw public responses are retained.

Measured build/run/tokenizer costs and raw failures are retained. Early manual
exploration, authoring, initial external startup/ingest and all historical model
costs were not measured completely. These unknowns are not zeros. Raw compiler
errors 9 and 12 and the missing-field trap 103 are described in `failures.md`.

**NOT_RUN:** held-out development tasks against the current direct-search agent
workflow. The two corpus fixtures are implementation fixtures, not a held-out
development benchmark. Do not relabel lookup tests as development-task results.
Existing L775/L776 must freeze and independently admit tasks before a paired
run; preserve every attempt, failure, retries, token counts, retrieval/index/
tokenizer/verification/model costs, wall/CPU/memory, and shared-device attribution.
Complete-cost qualification must refuse missing observations. No speed or
quality comparison is available from this candidate.

Dependencies remain under L408: FR-1 file operations, FR-13 arguments/environment,
FR-14 child-process execution and isolation, FR-15 clocks, FR-16 sockets/readiness.
L690 supplies the future native authenticated transport; issue 648 and the
existing adapter owners supply trusted session/model context. The packet component
can be reviewed now; deployed retrieval and the requested full qualification stay open.
