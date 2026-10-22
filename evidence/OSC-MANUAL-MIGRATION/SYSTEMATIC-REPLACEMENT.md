# Systematic legacy replacement with seeded Omega

Execution contract for L417/L495; direct implementation slices L550 (compiler/OSC) and L332 (boot/driver). This extends the existing retirement program, not a second competing program.

## Binding scope

Personally handwrite replacements in the new seeded Omega. No automated C-to-Omega translator is the implementation method. No generated C, C-backed runtime, legacy OSC compiler, Rust launcher, or renamed wrapper qualifies. Every production compiler is written in Omega and compiled by an Omega compiler whose ancestry reaches the handwritten seed. C interoperability stays outside that ancestry, at the converter boundary.

The replacement surface includes production code, build executables, launchers, test executables, operating-system services and driver components. Historical evidence stays immutable. Third-party and firmware dependencies must be explicitly inventoried; retaining one is an open dependency, never a silently waived retirement.

## One dependency order

| Order | Deliverable | Existing work to reuse | Retirement proof |
|---|---|---|---|
| 0 | Reproducible seed and Omega-built compiler | L408 qualified source pins; preserve its ongoing R6 work | Reconstruct from encoded seed; match compiler hashes; independent behavior/refusal gates; no foreign compiler or linked runtime |
| 1 | Compiler capabilities needed by boot/native tools | Existing FR-1 through FR-17, not duplicate requests | Each required effect/layout/backend implemented in Omega with explicit boundary/refusal tests; no fallback to C |
| 2 | Boot entry, loader and recovery | Aienux existing boot code and recovery receipts, L420 | Native artifact boots on target, recovery verified, foreign boot component absent from the selected chain |
| 3 | Kernel foundations: memory, interrupts, scheduling and device access | Aienux/physics/atlas existing designs, L420 | Native Omega components in actual boot image, deterministic correctness and recovery, no old implementation called |
| 4 | GB10 initialization, memory mapping, queues and driver | L332/L346 source references; L390 hardware gates | Working Omega GPU execution without NVIDIA host modules; golden output, holder checks and verified rollback; signed firmware separately tracked |
| 5 | GPU compiler/runtime/libraries | CUDApp, Omega runtime, L419 | Same functional contract on chip; seed-native compiler ancestry; no legacy library in executable closure |
| 6 | Model engine and service/runtime layer | sovereign-core, protocols, aegis-runtime, spark-rsi; L495/L496 | Real callers migrated, state/data compatibility, production swap/rollback and source-to-binary proof |
| 7 | Remaining tools, shell, benchmarks and interplane | Existing catalog and native om/omg work | Actual invocation paths point to qualified Omega artifacts; old production dependencies removed |
| 8 | Build/test orchestration and remaining host floor | Existing shell/Python/C/Rust inventory | End-to-end boot/build/test path runs on Omega-native replacements, historical oracles not runtime dependencies |

This is dependency order, not permission to postpone the driver indefinitely: implement the minimum compiler and boot capabilities that unblock the driver first. Pure source preparation can proceed before hardware is available; hardware retirement cannot.

## Per-component state machine

`INVENTORIED -> CONTRACT_PINNED -> HAND_PORTED -> NATIVE_VERIFIED -> CALLERS_MIGRATED -> CUTOVER_VERIFIED -> LEGACY_ARCHIVED`

A standalone primitive stops at NATIVE_VERIFIED. An archive tag alone is preservation, not LEGACY_ARCHIVED retirement. A simulator result does not satisfy a hardware gate. A native executable with no actual caller switch is not a retired library.

Each record binds: original repository/commit/path/hash; actual consumer(s); replacement source/hash; seed compiler/hash/ancestry; native executable/hash; build and runtime dependency closure; behavior/refusal and compiled-mutant receipts; cutover target/readback; recovery evidence; final archived source identity. Unknown provenance blocks retirement.

## Immediate compiler/OSC slice

The existing L550 `seed/native/literal.om` is an isolated, handwritten integer reader. It is NOT an integrated compiler replacement. Do not keep adding disconnected miniature ports and count them as retiring OSC.

1. Reconcile `legacy-inventory.json` against branch-only compiler changes and actual production callers. Preserve exact source pins, do not overwrite historical receipts.
2. Compare each required compiler behavior with the existing seed compiler. Reuse already implemented parsing, type checking and native code generation; port only missing behavior. The OSC syntax itself is historical, not a second production language to preserve.
3. Do not silently introduce hexadecimal/binary literal syntax into the seeded compiler: the isolated reader's transport contract is not a language-spec amendment. Confirm the native spec and exact consumer first.
4. Identify the first complete compiler/tool consumer that can move without unresolved FRs. Port its full contract by hand, test refusal and output behavior, integrate its actual caller, then remove the legacy dependency.
5. Coordinate compiler source edits with the existing single owner. No duplicate worker or edits to its mutable R6 worktree; this chat implements its assigned replacement code personally.

## Coverage and reporting

Use the existing L496 twelve-repository catalog and artifact manifest as a historical starting point, not a fresh live assertion. Add exact source snapshots and read back deployed paths before reporting current retirement counts. The catalog does not prove that every repository or every runtime dependency has been discovered.

Report source-port progress, isolated verification, caller migration and production retirement separately. Count only CUTOVER_VERIFIED components as production replacements. Never equate zero matching source filenames with zero C/Rust in a binary. Measurements remain public, and speed claims need measured evidence, not operation counts alone.

Existing owners and unrelated work are not stopped by this document. No system has been rebooted or cut over by publishing it. GPU/module changes retain the hardware gate ladder and recovery requirement.
