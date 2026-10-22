# L489 batch acceptance, issues 612 through 607

Ledger owner L489; coordinating parent L487; integration desk L488. No issue closure is claimed. Independent review is pending. All model-like traces in this batch are explicitly synthetic. The implemented code is optional Linux host tooling, outside the native engine. No live engine, authority, journal, training code, model, GPU, server, driver, service or cron was changed. No extra agents or GitHub PR/issue writes were used.

## Dependency map and reuse

Descending inventory is 612, 611, 610, 609, 608, 607. Implementation order is 608 archive/envelope, then 609 existing-format adapters and 610 read-only queries, then 611 local contribution bundles and 612 offline viewer. Issue 607 records coordination and unresolved cross-project work. The parent brief specifically consolidates the six issues on one isolated branch; the desk may split commits into issue proposals.

The checked base is a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a, equal to GitHub main at inspection. Existing branches/source, handoffs, issue discussions, PR inventory and live ledger were checked. L396 module split is reused as a boundary; no moved subsystem or lock is replaced. L478/L479 integration and L483/L484 bridge trees are untouched. docs/PROGRAM.md is absent from this base; decisions 16 through 24 were read from doc/program instead. Existing OSC bootstrap/external-byte and syscall studies were inspected. No language changes were introduced.

Reused interfaces: `src/searchtrace/st_hook.h` and existing corpus verifier/generator/hookcheck, `src/runtime/rx_argus.h`'s existing stream ABI, `docs/dual/DUAL_RECORDS.md` and the native canonical encoder/decoder, and existing program/machine digest identities. `src/runtime/rx_cortex.h` remains the sole canonical memory writer. No source archive or canonical journal is modified by inspection, sharing or viewing.

## Per-issue gates

PASS means the stated local host behavior was actually exercised. NOT_RUN means the gate remains open. PARTIAL is never issue closure.

| Issue / acceptance row | Status and evidence |
| --- | --- |
|608 reopen, truncation, corrupt length/version, ENOSPC, EACCES, symlink swap, unclean shutdown|PASS host: archive tests, explicit diagnostics, complete orphan recovery to new root, original bytes unchanged. Recovery preserves known losses and never resurrects retained-out data. ENOSPC is injected into actual filesystem-write seam; EACCES is OS permission denial.|
|608 offline write/query with outbound denied|PASS host: kernel seccomp policy, deliberate socket positive control EPERM, strace network-call audit, bounded worker through archive/query/preview/package/viewer/import/subprocess CLI. No listener.|
|608 deterministic canonical payloads/digests|PASS: closed schema, golden contribution vector, independent big-endian frame/SHA-256 oracle, source-byte mutation controls. No addresses enter the schema.|
|608 bounded memory/burst/non-vetoing producer and overhead|PASS local host queue: fixed message credits, bounded diagnostic/attribution tables, exact submit/persist/drop counts, disabled mode creates no files. NOT_RUN: representative CPU/GB10 overhead, native producer attachment and crash-time loss quantification.|
|608 quota/age/pins/purge, unrelated Cortex/receipts|PASS host: explicit retention/purge/pin behavior; complete segments only, original loss summaries persist in index. Unrelated receipt canary unchanged. No encryption claim.|
|608 observability boundary|PASS schema/docs/behavior: evidence NOT_VERIFIED, no authority or training-control API. Independent evidence verification remains external.|
|609 disabled byte equivalence / producer I/O|PASS existing SearchTrace hookcheck on two frozen synthetic tasks. Host adapters do not touch native source. NOT_RUN: new in-process World/DUAL/authority off/on attachment and producer syscall equivalence.|
|609 candidate/realization/authority reconstruction|PASS host import: existing native corpus verified by pinned verifier; causal candidate/realization pairs, program/machine refs; ARGUS outcomes/critical losses; DUAL advisory choice and referenced digest preserved. Native encoder oracle supplies independent ARGUS/DUAL fixtures. NOT_RUN: installed native bridge and actual training correlations.|
|609 missing/invalid metrics and clocks|PASS host: unavailable/null metrics, model rcycles estimated; code_bytes distinct from memory footprint; unknown enum/version/unit/nonfinite/overflow refused; DUAL signed predictions and zero resource IDs match native contract. No trace-created calibration status or authority.|
|609 full ring/drop/nesting/thread-exit/collisions/overflow|PASS bounded host queue drop/attribution/overflow checks and timestamp collisions. PARTIAL: native hook nesting/thread locality contract reused without changes. NOT_RUN: newly installed native callbacks, native thread-exit/ring-sticky-loss suite under that bridge.|
|609 overhead raw host/GB10 instrumentation evidence|NOT_RUN: explicitly forbidden timing/GPU workloads. No speed/energy/overhead claim.|
|610 frozen ordering/totals/parent links/corrupt index/segments|PASS synthetic multi-producer fixture, exact ordering/typed totals, missing/cross-run parents qualified, filtered parent refs retained; malformed or missing data inconclusive.|
|610 determinism/qualified comparison|PASS byte-stable query outputs, machine/build/config/precision differences visible. Unknown precision or missing/incomplete runs cannot claim direct comparability.|
|610 read-only CLI while tracing disabled / offline|PASS all seven requested commands, strict option/arity validation, source-byte invariance, kernel-enforced offline integration. Product requires installed Node 24/Linux procfs, no external service/package downloads.|
|610 unavailable energy / evidence boundary|PASS typed provenance, separate units; unknown/estimated energy is unavailable; integrity never claims receipt verification.|
|610 query scaling measurements|NOT_RUN: timing runs forbidden. No database service dependency.|
|611 deterministic exact bundle/digest|PASS frozen golden vector, byte-identical preview/package, content_hash and exact-file SHA-256 definitions. Explicit run and field allowlist.|
|611 canary prompts/credentials/training/paths/tensors/metadata|PASS raw fields refused, including integrity-correct hostile segment containing binary/metadata canaries. Closed output construction, typed values, pseudonymized identities and wall-time removal.|
|611 unknown schema/fields, symlink/path escape, truncation, unowned source|PASS fail-closed owned-directory/file checks, malformed-version/index/segment rejection and source schema validation.|
|611 no network syscalls; preview matches package|PASS end-to-end inside socket-denying kernel filter; strace records zero network syscalls in the complete workflow, including packaging. Original diagnostic RED identified Node subprocess capture socketpairs, removed without relaxing denial policy.|
|611 source immutable / original loss/qualification retained|PASS source hashes unchanged; loss disclosures survive step/status filters and retention. No upload, training permission, consent or signature operation exists. Open Humanity independent validation/signing/transport NOT_RUN and separately owned.|
|612 offline bundled assets/no listener|PASS generated self-contained HTML, CSP hashes, connect-src none, no remote scripts/CDN/login/server, no automatic browser launch. CLI fallback unchanged.|
|612 filters/steps/time/compare/missing/loss interaction|PASS actual bundled client script exercised with in-memory DOM fixture, plus deterministic artifact snapshots and filtering/causal links. NOT_RUN real-browser rendering and accessibility qualification.|
|612 cannot mutate archive/consent/authority/model/share|PASS source invariance under viewer generation; client has no mutation or network interface. Browser controls operate only on embedded typed query values.|
|612 Spark CPU/memory/query-latency footprint|NOT_RUN: performance/timing runs forbidden. No invented footprint numbers.|
|607 disconnected representative DGX training workload|NOT_RUN: no model/GPU/training workload authorized. Synthetic host behavior does not substitute for this gate.|
|607 cross-project consent/receiver/replay/tamper checks|NOT_RUN: aien-train #1, SC #700, Open Humanity #26/#27/#28 remain separate owners. Local tamper refusal passes; no claim of signed publication or receiver acceptance.|
|607 benchmarks/host-versus-GB10 qualifications|NOT_RUN timings/GB10/energy, explicitly disclosed in every host receipt.|

## Native observer blocker and next action

The installed hook ABI is a C function pointer taking context and StEvent pointers, with thread-local installation and nested realization behavior. The qualified OSC native ABI at this base requires OscRt in x7; byte slices consume pointer/length registers. No compatible qualified callback bridge was found. The separate osc/syscall branch adds raw syscall support, not this bridge. A new C/Rust/GNU execution-path implementation or a language change is outside this batch's authorization.

Next native step: the existing language/runtime owner must qualify an observer callback bridge that supplies runtime context, copies only stable typed values into the existing consumer path, preserves TLS/nesting/thread-exit/non-veto behavior, and is checked against frozen off/on results. Then one shared hardware/training run through the existing owner can cover representative overhead, drops and actual training correlations. L489 does not claim those gates passed.

## Review entry points

`tools/trace/README.md` documents format, migration, controls and CLI. `tests/trace/run.sh` is the host gate, including independent native fixtures and strict network denial. `tests/trace/mutants.mjs` runs behavioral mutations in private temporary copies. `evidence/L489/README.md` pins exact logs, hashes, commands and counts. The desk owns PR creation, review, integration and issue disposition. L489 remains open for parent verification.
