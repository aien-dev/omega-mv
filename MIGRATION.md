# Replace legacy first-party code with seeded `.om`

Directive: the final state is seeded `.om` throughout the project's first-party implementations,
including runtime, compiler, services, build/release tools, automation and project-owned tests.
There is no permanent legacy, wrapper or dual production path. For each component: implement and
qualify its `.om` replacement, cut consumers over, then delete the superseded implementation and its
obsolete dependencies. Consult old source in Git history; do not keep an active legacy directory.

Independent third-party oracles may stay outside the delivered source/build/runtime closure with
their identities disclosed. Project-authored oracles are migrated too.

## Current inventory

This checkout, counted at the cutover commit:

| Area | Legacy now | Notes |
| --- | ---: | --- |
| `src/` | 272 C, 254 headers | runtime, OSH, authority, compiler C path, numeric kernels, services |
| `tests/` | 238 C, 35 headers, 3 Rust | project test harnesses and differential twins |
| `tools/` | 51 C, 10 headers | emitters, acceptance helpers, build tools |
| `bench/` | 6 C | benchmark drivers |
| `seed/` | 3 C | reference runner, sm121 listing, remaining seed oracles |
| `seed/omc/omc.om` | 1 `.om` production compiler | builds all other first-party `.om` |
| Cross-repo | Rust/C | `spark-rsi-mv`, `open-humanity`, `aienux`, `aien-sovereign-core-mv` |

The count is large enough that this is a phased replacement, not one change. Every phase ends with
deleted legacy code and qualified consumers; a plan or a passing unit test is not a cutover.

## Order

0. **Stop new legacy.** New first-party implementation, runners and tests are seeded `.om`. Missing
   facilities go to the existing compiler owner, not a new C/Rust/Python helper.
1. **Seed test and tooling surface** (pilot complete): remove gcc-built C twins; use committed
   expected outputs and data fixtures; then replace `seed/check/runner.c`, `seed/sm121/sm121list.c`
   and the remaining seed shell coordinators with `.om` coordinators.
2. **Authority and effect runtime**: `src/runtime/rx_capq`, `rx_caproot`, `rx_semantic`,
   `src/osh/host/osh_exec.c` and their consumers. The `.om` replacement must carry grant identity,
   generation, revocation, durable state and dispatch; the strict `execpure` launcher is the first
   effect boundary. Delete the C authority path only after every consumer is cut over.
   Status: the action, delegation and reuse transition contracts are promoted to
   `seed/authority/` and pass 219/074/074 checks; the durable 32-word record store with fsync,
   atomic rename and torn-file refusal passes 77 checks. The dispatcher persists in-flight, runs a
   synthetic effect, records performed/unknown, and marks unknown when the generation changes during
   the effect (37 checks, including /bin/true and a static strict-profile exec). Real-effect dispatch, a cross-process lock and consumer cutover are next;
   the C owner remains active.
3. **Trace and privacy (#607)**: `rx_argus`, trace producers and the Open Humanity consent lane.
   Status: the local `.om` segment writer (`seed/trace/segment.om`) is qualified with 117 checks:
   header, append+fsync, complete-record scan, short-tail quarantine and fresh-segment recovery.
   Exact-byte consent/export and the receiver contract are `NOT_RUN`; they need a `.om` digest.
4. **Evaluation and promotion (spark-rsi #67)**: replace the Rust promotion/evaluator gate with the
   `.om` owner; keep sealed tasks outside the proposer's writable surface.
5. **Device and DMA (#672)**: `.om` device contracts and queues. Firmware and hardware are outside
   the delivered source closure; physical qualification stays `NOT_RUN` until reserved hardware is
   exercised.
6. **Build, release and automation**: Makefile targets and shell gates become `.om` coordinators;
   remove shell/Python orchestration from the delivered path.
7. **Compiler trust**: the C reference path is replaced or demoted to an external oracle outside the
   closure; add the independent route required for diverse double compilation.
8. **Cross-repo services**: migrate the named repositories under their own owners and ledgers.

## Cutover rules

- One owner and exact paths per component; no second compiler, runtime, authority or store.
- Freeze source/seed/compiler/binary/config identities and record raw commands and exits.
- Qualify the replacement, cut every consumer over, then delete the superseded implementation.
- Preserve Git history and immutable evidence; do not rewrite history or keep dead source active.
- Missing hardware, an independent compiler route or an authority owner is `NOT_RUN`/`BLOCKED`,
  never `PASS`.

## Pilot completed

`seed/rung4/tests/{fib,sort,bytecopy}.c` were gcc-built twins of the `.om` programs. They are
deleted. `fib.want`, `sort.want` and a deterministic `in.bin`/`bytecopy.want` are committed;
`gate4.sh` now compares the `.om` output with those fixtures and does not invoke gcc for the
program tests. `gate4` passes with 217 cases, including the expected-output checks. The C reference
runner remains for a later phase because it links the legacy C path and is an oracle, not a
production component.

## Known `.om` capability gaps for later phases

- Multiple source files/imports for service-sized programs.
- Structured records and bounded maps for policy, grants and receipts.
- Durable atomic state primitives beyond single-file create/rename.
- Process supervision/process-tree budgets for the authority dispatcher.
- Threads or event loops for long-running services (the current host words are synchronous).

These are dependencies for the existing compiler/runtime owners. They are not exemptions for
keeping C or Rust.
