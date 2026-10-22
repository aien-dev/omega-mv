# Seeded retirement integration, L496

The complete TRN1 schema-1 verification utility from `aien-replay` now runs as a
22,600-byte native Omega executable. It verifies transcript records, computes
SHA-256, preserves refusal order and codes, emits accepted metadata, compares
replays and resolves subsystem names. The executable has no linked libraries,
interpreter, subprocess or call back into Rust/C. Its remaining platform is the
Linux AArch64 read/write/exit system-call interface.

**Production retirement is PARTIAL, with zero verified retirements.** The chosen
original is an entire Rust library utility, not an existing standalone CLI. Its
full verifier behavior is implemented; the new framed transport does not replace
Rust's ABI or migrate its callers. This distinction was frozen before coding in
[the contract](TRN1_CONTRACT.md), the handoff and L496. Acceptance of this utility
selection and production consumer migration remain review gates. No existing
source, installed command, service or GPU workload was changed.

## Native command and exact-source replay

The tested isolated command is `build/retirement/isolated/active`, a symlink
directly to the native ELF. For example, from this repository:

```sh
printf 'S\000\000' | build/retirement/isolated/active
```

It prints `transcript`. The [contract](TRN1_CONTRACT.md) specifies verification,
comparison, accepted-record metadata, framing, limits and errors. The native
source and preserved licence are in [seed/retirement/trn1](../../seed/retirement/trn1/).

On Spark, with the local repositories, pinned git objects, C/Rust test toolchain,
offline Cargo cache, `strace` and `quiet-build` available, replay everything with:

```sh
bash tools/retirement/verify.sh
```

`prepare.sh` extracts the exact seed source into `build/retirement/seed-src`
and calls the existing `seed/omega-from-seed.sh`. `seed-suite.sh` calls the
original `seed/omc/gate5.sh` there. No compiler-owner file is edited in a source
worktree. [pins.sh](../../tools/retirement/pins.sh) fixes the original Rust
source, protocol corpus, independent interpreter and compiler inputs.
`RETIRE_SC_REPO` and `RETIRE_PROTO_REPO` can locate the two original repositories.
No network fetch is hidden in the replay scripts; inventory snapshots refer to
local remote-tracking refs. The published snapshot follows successful fetches of
all 12 catalog repositories, recorded in `fetch.json`.

The original `aien-replay/src/lib.rs` and original conformance test are extracted
unchanged. A test-only adapter exposes their functions through the new transport.
The scratch Cargo manifest retains `sha2`, excluding unrelated KV-cache dev
property tests; the effective lockfile and Rust toolchain are recorded. This is
the original verifier implementation, not a reimplemented Rust oracle.

Python and shell perform inventory, tests and isolated installation only. C builds
only the independent test reference and the preexisting seed-suite oracles.
None of those programs executes on the installed native verifier's path.

## Gates and evidence

All evidence below is under [evidence/seed-retirement](../../evidence/seed-retirement/).
The logs and JSON reports contain exact source/executable/compiler hashes.

| Gate | Result | Evidence |
|---|---|---|
| Absent native executable | Expected RED | `red.log` |
| Original Rust conformance test | 77/77 | `rust-suite.log` |
| Original + current corpus + generated differential cases | 1284/1284 | `trn1-summary.json`, `trn1-cases.json`, `green.log` |
| Independent C schema-1 reference | 1260 comparisons | `trn1-summary.json` |
| Independent Omega source interpreter | 3 original cases | `trn1-summary.json` |
| Repeated requests | 1276 requests, twice per executable | `trn1-summary.json` |
| Fragmented pipes | PASS | `trn1-summary.json` |
| Faults, live replies, request limit, SIGPIPE | 10 cases | `transport.json`, `transport-red.log`, `transport-green.log` |
| Compiled SHA/count/version/state mutants | 4 compiled, 4 rejected | `mutants.json`, `*-mutant.log` |
| Original seed gate | 177 cases, including 4860 nextfuzz inputs | `seed-suite.log` |
| Seed regeneration/fixpoint | Qualified compiler reproduced | `seed-birth.log`, `seed-hash.log` |
| Isolated installation and rollback | 8 checks, including deployed original 77 | `deployment.json`, `deployed-corpus.log` |
| Retirement policy negative controls | 12 checks | `policy-tests.json` |

The generated cases cover every record type, length/refusal precedence, all
single-byte changes and truncations of a representative transcript, 4096/4097
records, full annotations, SHA block padding, extreme crumb parent IDs,
ARGUS continuity, comparison and registry edges. Per-case evidence records
stdin/stdout/stderr hashes and exit status. The C file CLI uses different process
exit conventions, so its semantic output is compared and its raw exit recorded.
The independent interpreter checks three cases, not the whole corpus. Current
protocol main has schema 3; the original Rust reader rejects it. This port
preserves that rejection and makes no schema-3 support claim.

`deploy.py` is a test installer confined to `build/retirement/isolated`. It checks
source/test/executable/seed hashes and ELF closure, stages content-addressed
releases, locks its own directory, writes a durable transaction journal, swaps an
atomic symlink, and smokes the new command. Tests start with the actual Rust
oracle, switch to native, roll back to Rust, inject a failed health check, recover
an interrupted transaction, then leave native active. A normal-path syscall trace
shows only read/write/exit after the initial exec; this sample trace supplements
the source and birth-chain checks, rather than proving all paths by itself.
This is isolated integration evidence, not a production service swap.

`policy.py` recomputes artifact counts and requires evidence files/hashes for
passing gates. It rejects a whole-system retirement claim with remaining legacy,
incomplete coverage, invented counts or unfinished stages. It also rejects using
isolated evidence to mark a production artifact retired. A normal policy audit
returns 0 for a consistent PARTIAL report; `--require-retired` deliberately
returns 1 today. Invalid claims return 2. These are audit consistency checks,
not cryptographic attestations of a human-entered production claim.

## Current inventory and ownership

[retirement-manifest.json](../../evidence/seed-retirement/retirement-manifest.json)
contains the observed executable paths/hashes, process IDs, ELF dependencies,
source commits, build records, replacement owners and exact cutover gates.
The snapshot finds 32 installed or running first-party-named ELF paths, including
6 running paths. Its 34 legacy rows also include source/feature artifacts; that
number is neither a process count nor a count of proven production consumers.
Installed artifacts without exact build provenance remain `UNRESOLVED`.
Compiler `.comment` strings are observations, not a birth-chain proof.

The Motor at `~/.local/state/aien-q36/bin-19173f9e/aien-serve-http` remains Rust
built by Cargo, linked to the C-built Omega archive. Its own `BUILD.txt` identifies
sovereign-core `f0230a59` and Omega `6de999da`; the manifest pins full commits,
ELF hash, build-record hash and source build-file hashes. Dynamic dependencies
are observed; a complete static closure is not yet proven. Nothing was sent to
its service endpoint. The running cockpit and mail release also has matching
`SHA256SUMS` and `source.commit`, pinning sovereign-core `0df7c321c150`; its
Cargo manifests, source/build hashes and direct dependencies are recorded.
These deployment records do not substitute for a reproducible binary rebuild.
Bridge `77376b1` emits C/Python and is not native migration.
Attachments `7916bcc` retain a C host boundary despite their native policy code.

The inventory reuses **L418's original `tools/c2om/repos.tsv`**, adding relevant
Rust repositories. Its original C equivalence script is specific to C/toolchain
artifacts; it cannot expose the Rust library API. The small TRN1 harness therefore
reuses the original shared corpus, original Rust test and existing seed helpers.
There is no second C translator. L418's dirty files are untouched.

Source counts below are computed from git blobs on fetched default branches,
not from dirty worktrees. C includes headers. Path-based categories separate
tests, historical evidence/oracles and third-party trees; they do not prove which
source is linked into an installed program. Other organization repositories are
not fully qualified, so `coverage.complete` stays false.

| Repository | C implementation | C tests | Rust implementation | Rust tests | Historical C/Rust | Third-party C/Rust |
|---|---:|---:|---:|---:|---:|---:|
| omega | 183270 | 126493 | 12591 | 28 | 47353 | 0 |
| aienux | 68547 | 40546 | 65457 | 7308 | 0 | 632030 |
| CUDApp | 28075 | 17025 | 0 | 0 | 2095 | 37334 |
| physics | 9601 | 2848 | 0 | 0 | 0 | 37334 |
| benchmarks | 0 | 0 | 8030 | 510 | 3084 | 0 |
| atlas | 1299 | 0 | 0 | 0 | 0 | 0 |
| interplane | 0 | 50 | 20127 | 11743 | 0 | 568 |
| sovereign-core | 0 | 10 | 200437 | 88188 | 0 | 0 |
| protocols | 3010 | 0 | 3646 | 1573 | 0 | 0 |
| aegis-runtime | 0 | 0 | 9258 | 4012 | 0 | 0 |
| spark-rsi | 0 | 0 | 23140 | 8347 | 153 | 0 |
| omega-bridge | 0 | 0 | 0 | 0 | 0 | 0 |

## Remaining cutover stages

| Stage | TRN1 candidate status | Owner / remaining work |
|---|---|---|
| Executable equivalence | PASS, isolated | L496 evidence; review whole-library selection and new transport |
| API/ABI and data migration | NOT RUN | L495 identifies real callers; L408 FR-1/FR-13 for files/argv, FR-7 plus an explicit stable caller interface for in-process use; Rust ABI is not C ABI |
| Isolated deployment | PASS | L496 staged native command and original-corpus smoke |
| Shared GPU/model train | NOT RUN for system | Authorized coordinator / L384 / L387 only; verifier itself uses no GPU/model |
| Dependency closure | PASS for native verifier, system unresolved | L495 exact installed build/source proof; L419 C runtime/GPU; L420 kernel/platform |
| Transactional swap and rollback | PASS, isolated only | L387 or designated deployment owner must authorize and verify any actual consumer switch |
| Main source/oracle archiving and smoke | NOT RUN | Renda owns review/PR flow; no deletion or merge by L496 |

Compiler owner **L408** retains all language/backend work: FR-1 files, FR-3
imports, FR-4 floats, FR-5 memory/device syscalls, FR-6 threads/atomics, FR-7
shared-library ABI, FR-8 MMIO/privileged kernel operations, FR-9 x86, FR-13
arguments/environment, FR-14 spawn/wait and FR-15 clocks. A precise TRN1 consumer
supplement was appended without duplicating those FRs. The live server audit
also identified missing socket/readiness and signal-disposition support, now
requested as FR-16 and FR-17 with isolated test gates. The current TRN1 transport
inherits Linux signal behavior, including default SIGPIPE on a closed pipe.
No compiler feature is claimed implemented by this port.

**L418** retains C toolchain and waves 1/2; **L419** C GPU/runtime; **L420**
kernel/platform; **L494** calculator/symbolic math; **L483** bridge; **L484**
attachment host migration. **L495 remains open** for actual production caller
replacement, complete lineage/closure and all-system retirement. These changes
are ready to review as a verified native utility and isolated integration slice.
They do not satisfy an existing CLI drop-in or full production retirement claim.
