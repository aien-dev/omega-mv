# Contributors

This project is built by AI models, orchestrated by Drake Stapleton, who wrote none of the code.
Every model below did real work. A model is listed only for work it did. The orchestrator checks all output before it merges.

| Model | What it did |
|---|---|
| Codex | Seeded-program teaching guide and explicit legacy identity/receipt boundary (issue #619); static documentation checks only |
| Grok 4.7 | Merged current main (`089e5b053`) into #936. Conflicts were generated crumbs (`.crumb`, `tests/.crumb`); regenerated them with the crumb.lock tool (`d0f0b95d`). |
| Grok 4.7 | Restored the offline DAG evaluator's forbidden-symbol guard from #590 (`3bd56731`): generation store, authority, world, and calls that map or run code. |
| Grok 4.7 | Imported the offline DAG evaluator `tools/dagsim` verbatim from closed PR #590 (`3bd56731`) and wired a host `test-dagsim` target. Analytical fixture checks only. No validated cost claim. |
| Codex (GPT-6.1 Sol) | FR-9 x86-64 Linux backend integration from historical 7eca2ac3f, full-language lowering, contract-loop/output-capacity/dead-branch repairs, division/shift proof-aware diagnostics, original-conductor and historical-mutant qualification repair, independent disassembly and cross-architecture qualification; orphaned-lane recovery, current mutation re-anchoring and x86 OM-2 emitted-storage bound |
| Codex (GPT-6.1 Sol) | TileLab decimal-overflow and malformed-register-budget refusal repair, host regression fixtures and semantic negative controls (TL907-01) |
| Codex (GPT-6.1 Sol) | Corrected qty prefix-query types and capacity-only diagnostic repairability, with exact-record regression controls (#911) |
| Codex | Inference-cost driver compiler-failure status repair, host-only refusal regression probes and independent review; native Omega execution NOT_RUN |
| Codex (GPT-6 Astra) | Bounded native peer credentials, multi-descriptor polling, single-iovec sendmsg, socket-option allowlist and host fixtures (#788) |
| Codex (GPT-6 Astra) | Seeded IR statement/type/scope validation repairs, bootstrap-safe helper splits, and independent C-oracle regression qualification |
| Codex (GPT-6 Astra) | Strict seeded-Omega HTTP/1.1 server/client with SSE and strict RFC 8259 JSON reader/writer, with oracle parity, seeded fuzz, mutants, fd-leak/timeout and curl SSE demo (#788) |
| Codex (GPT-6 Astra) | Seeded memory-profile wire specification, preserving historical OSC versions and explicit backend qualification boundaries |
| Codex (GPT-6 Astra) | Seeded Omega OSC IR v1 to OpenCL Kernel SPIR-V backend, CPU differential gate, demo and CFG/trap review fixes |
| Codex (GPT-6 Astra) | Seeded executable contracts, loop-head invariants, release erasure, purity and runtime refusal controls |
| Codex (GPT-6.1 Sol) | Corrected boolean bitwise typing in seeded OSC IR output, with C-referee regression cases |
| Codex (GPT-6.1 Sol) | Seeded Omega OSC IR v1 to WebAssembly MVP backend, native differential gate, refusal and mutant controls |
| Codex | Handwritten bounded PD0 stream primitive and binary consumer in Omega, native tests, and host-reference/source review (native execution NOT_RUN) |
| Codex | Current .om front-page status and source-backed C migration inventory |
| Codex | BUILD receipt cleanliness enforcement and optional diagnostic pointer regression fixes |
| Claude Opus 5.5 | Numeric GB10 operations (LDS_STS, REDUCE_SUM), the independent CPU oracle, the Gate 5 qualifier and its receipts, and the fixes from review |
| Codex (GPT-6.1 Sol) | Code review of the numeric changes (patch checks, receipt blobs, GPU lock handling) |
| Gemini 3.8 Flash | Code review of the numeric changes (patch checks, qualifier script, build rules) |
| Codex | Numeric uncertain-completion retention, exact completion checks, and simulated-driver lifecycle regression tests |
| Codex | Private exclusive sm121 reference outputs and bounded source-bound host safety checks |
| Codex | C5 kernel-dump private exclusive creation, failure handling, and source-bound host safety checks |
| Codex | C6 private kernel dumps and temporary listings, bounded dump filenames, descriptor-safe readback, and source-bound host safety checks |
| Claude Opus 5.5 | Ionic-droplet reservoir (#440): pinned reference, CPU simulator and readout, `.mat` reader, Gate 2 comparison, benchmarks, timing and receipt |
| Claude Fable 5.1 | CodeQL high-severity fixes in the OM-2 oracles: 0600 image creation in offsetof.c, fixture-name allowlist and truncation check in sha_qual.c |
| Claude Opus 5.5 | Omega 1.0 roadmap: source-pinned readiness audit of the seeded compiler, IR, WebAssembly and bridge paths, milestone acceptance gates, tracking issues #790-#800, OM-2 contract files brought to main byte-identical, and stale status text corrected |
| Claude Opus 5.5 | Function-mode constants in the seeded compiler (`fnstore`): consts accepted without `fn main`, globals/texts/host words still error 9, gate16 byte-identity fixtures and old-compiler/mutant negative controls |
| Codex (GPT-6.1 Sol) | Repaired qty float-domain classification and checked ingress, diagnostic categories and query/output streaming; permanent adversarial regression fixtures for the #909/#911 audit |

This table starts with the numeric work on this branch. Earlier work in this repository is not yet attributed here; add rows only for work that can be traced to a model.

Update this table in the same commit as the work it credits.
| Claude Opus 5.5 | Seed-chain bootstrap challenge: `seed/challenge/run.sh` rebuilds omc from the 408-byte seed out of a pinned `git archive` and checks every input and rung against a table read from committed files, with tampered-rung and wrong-value negative controls |
| Claude Opus 5.5 | Seeded compiler: an empty `text e ""` (length 0) kept sized instead of read as an unsized view (`unsz` at the element, view-argument, `part`, read/write and `len` sites; #775), s4 regression rows, a reintroducing mutant and the 2c824805 negative control |
| Claude Opus 5.5 | Seeded compiler: a function name of 9..63 bytes whose first 8 bytes spell a keyword (`continue_x`, `truncate_number`) accepted instead of refused error 16, and an over-long field name reported as error 13 before the keyword test; s1 regression rows, a reintroducing mutant and the 2c824805 negative control |
| Claude Opus 5.5 | Seeded compiler: `pread` needs a writable array (error 20) and `pwrite` takes a text, the writable check moved from code 162 to 161 in `hpread`; s4 regression rows, a reintroducing mutant and the 2c824805 negative control |

| Codex | Integrated compiler batch #904 with the quantity and diagnostic audit on current main; rebuilt compiler and bindings, refreshed pinned checks, and repaired the recursive-inline mutant anchor without changing its obligation. |

| Codex | Integrated FR-9 with compiler batch 3 and the latest quantity/diagnostic audit; preserved empty text and integer-bitcast behavior on x86 and kept seed bootstrap within its function budget by removing unused helpers and inlining one single-use helper. |
| Codex | Fixed x86 quantity unary refusals and empty-text emission; added explicit per-target acceptance contracts, preserved historical failed experiments, and exercised missing diagnostic callers and stale-binding controls. |
| Grok 4.7 | Merged current main (b3dca0f1) into #514. The only conflict was the generated root `.crumb`; regenerated crumbs with the crumb.lock tool (d0f0b95d). |
