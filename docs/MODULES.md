# Omega as modules: the map (L396, ADR 0039)

Drake, 2026-10-21: "What else can we pull out of Omega? I want to make this a true Unix-style repo where
we're going modular." ADR 0039 (aien-architecture `adr/0039-one-thing-per-repo`) is the rule: each repo
does one thing, and a repo uses another only through a lock file that pins one exact commit, the way
`physics.lock` and `aienos.lock` already work here.

This page is the plan. It says what each part of Omega is, what it uses, which parts can leave first, and
what must be untangled before the rest can leave. The map comes from a script, not from reading by eye:

    tools/modules/graph.sh [out-dir]        # groups from tools/modules/groups.tsv, under one second

It reads every tracked file and records four kinds of link: a C `#include`, a path named in the
Makefile or `mk/*.mk` (what gets built and linked), a path named in a script, and a path named in a doc.
Only the first two count for the cut order: they are the links that break a build if a file moves.
Numbers below are from `origin/main` a8ffd2b9 (14,258 files).

## 1. The modules

| Module | Repo | What it is, in one sentence | Files | Uses (pinned below it) | Licence |
|---|---|---|---|---|---|
| evidence shelf | `aien-dev/omega-evidence` | The receipts: logs, measurements and verdicts every past gate wrote, kept append-only. | 11,078 (199 MB) | nothing | AGPL-3.0 |
| crypto | `aien-dev/aien-crypto` | SHA-256, BLAKE3, SHA-512 and Ed25519 in plain C, no heap, no clock. | 11 | nothing | AGPL-3.0 |
| research shelf | `aien-dev/omega-research` | Research that nothing builds or runs: `research/m15`, `research/r14-failure-modes`, `research/dirac-oracle`. | 155 | nothing | AGPL-3.0 |
| omega (stays) | `aien-spark/omega` | The language and its runtime: Omegese, OSC, the IR, checker2, the CPU backends, program/resolve/realize/verify, the Visor session contract, the resident runtime (rx_*) with its Fabric test transport and the ALLEN binding, the numeric semantics. | 784 | crypto, evidence, physics, aienos | AGPL-3.0 |
| CUDA++ | `aien-dev/CUDAplusplus` (L395) | The GPU half of the runtime: kernel builder, encoder, submit, sessions, GPU libraries. | L395's map | a byte-identical copy of the shared base, no pin on omega | MIT (the one exception) |
| host | `aien-dev/omega-host` | OSH, the Linux host adapter: capability enforcement behind the aien-architecture#158 ABI. | 132 | omega, crypto | AGPL-3.0 |
| learn | `aien-dev/omega-learn` | Tensors (M20), autodiff (M21) and training (M22) written on Omega. | 25 | omega, CUDA++, crypto | AGPL-3.0 |
| programs | `aien-dev/omega-programs` | The experiments and subsystems written on Omega that nothing else uses yet: turing, brownian, estimation, dual (+experiment), reservoir, physics0, crumbline, searchtrace, path, ans, polyglot, prime race, plus the Turing calibration experiments (`calibration/`, 124 files) and the atemporal AT0/AT1 experiments (`research/atemporal`, 404 files), which their make fragments build. | 687 + 528 | omega, CUDA++, crypto | AGPL-3.0 |

Rule from the brief for the last row: a subsystem gets its own repo once something outside Omega uses it.
A search of aien-sovereign-core, aienos, aien-train and physics found no outside user of any of them
(sovereign-core uses only `src/runtime`, which stays). So they leave together as one repo, one folder each,
and any one of them moves to its own repo with `git filter-repo` the day it gets an outside user.

Changes to the first-cut map, with the reason:

- Fabric stays in omega: the runtime's R13 test build links `src/fabric` (the loopback transport and stand-in
  authenticator behind `AIEN_TEST_BUILD`). The ALLEN binding (`src/allen`, the omega side of ARCH-0035) stays too:
  the runtime gate runs it.
- OSC programs that leave with a module stay as frozen compiler test input. checker2's differential test, the
  OSC IR decode test and the emitter round trip read every OSC program in the tree (`src/osh/*.osc`,
  `tests/physics0/osc/*.osc`). The shell loops skip a missing file without a word, so a move would quietly test
  the compiler on less. Each moved program is kept byte for byte under `tests/compiler/corpus/<module>/`.
- Numeric stays in omega for now. `omega_program_fp32.c` (the language) and `rx_omega.h` (the runtime)
  include `omega_numeric.h` and `omega_matvec.h`, and `omega_numeric.c` itself includes the GPU encoder. It is
  the language's number semantics, tied both ways. It leaves only after cut C3 below.
- Visor and the resident runtime stay in omega. `src/language/omega_lower.h` lowers into `visor.h`; the
  Visor links `rx_aegis.c`; the runtime includes the language core 33 times. They are one thing today.
- physics0 does not go into the physics repo. Omega pins physics (`physics.lock`), and physics0 includes
  the Omega core 10 times, so putting it in physics would make physics and Omega pin each other.
- `tools/exp445` is the Omega emitter's test (`mk/omega-emit.mk`), not a Turing tool, so it stays.
- `tools/quietlock` is Spark machine tooling (it guards measurement windows for every repo). It moves to
  the Spark tools home with the research shelf cut, or stays if no such repo exists (checked at that cut).

Existing repos checked before naming new ones (`gh repo list aien-dev`, 2026-10-21):

- `aien-receipts` holds the Spark judge's signed bundles (`<repo>/<sha>/bundle.json`). The evidence shelf is
  raw gate output, a different thing, so it gets its own repo instead of mixing the two.
- `physics0` exists, empty, retired 2026-10-03 with "Physics-0 learner work lives in omega under
  src/physics0". If physics0 ever leaves the programs repo on its own, it goes back there.
- `benchmarks-mv` holds measurement harnesses; the prime race (`mk/prime_race*.mk`, 2 files) moves there
  instead of into programs, if its harness has no Omega-only dependency beyond CUDA++ (checked at that cut).
- No Spark-tools repo exists; `spark-supervisor` is a service daemon. `tools/quietlock` stays until one does.

## 2. The graph (include + build links, after the corrections above)

Arrows point from the user to the thing used. Counts are include + build links (build-file comments not counted).

    programs: turing ------> omega 15, numeric 9, runtime 9, crypto 25
              brownian ----> turing 28, crypto 8
              physics0 ----> omega 9, turing 3, crypto 35
              polyglot ----> omega 10, numeric 36, crypto 6
              searchtrace -> omega 23, crypto 21
              dual --------> runtime 10, dual_experiment 13, estimation 3, crypto 17
              estimation --> crypto 13;  ans -> estimation 3, crypto 1
              reservoir ---> numeric 4, CUDA++ 4, crypto 6
              crumbline, path -> omega, crypto;  prime race -> CUDA++ 8
    learn -----------------> numeric 20, CUDA++ 8, crypto 9
    host ------------------> omega 17, runtime 6, crypto 6
    CUDA++ ----------------> omega 23, numeric 13, runtime 5, crypto 20
    runtime (with fabric, allen) -> omega 36, numeric 4, CUDA++ 5, crypto 50
    numeric ---------------> omega 12, CUDA++ 4, turing 1, crypto 2
    omega (language + visor) -> numeric 2, crypto 40, physics 6
    crypto ----------------> nothing

Leaf-first order printed by the script (`order.txt`): crypto, then the knot
`runtime visor omega cudapp numeric turing`, then every other group, each on its own. Turing is in the knot
only through cut C4 below; before build-file comments were skipped, polyglot and allen showed in it too.

## 3. Loops to cut first (an interface, written down, before the files move)

The knot above is the only loop. Its upward links, and the cut for each:

| Cut | Link that closes the loop | The interface that cuts it | Owner |
|---|---|---|---|
| C1 | CUDA++ uses omega (21) and omega's runtime/numeric use CUDA++ (`omega_numeric.c` includes the GPU encoder, 4; `tests/runtime/r15_gpu_load.c` and friends include `rx_resident_gpu.h`, 5) | L395's map decides (its handoff, plan v1): the Omega bridges stay in omega (`omega_blackwell_gates.c`, `omega_accelerator_world.*`, `omega_evidence.*`, `omega_physics_dir.h`); a shared base (`sha256`, `omega_types.h`, `omega_vector`, `omega_core`, `omega_canonical`) is carried byte-identical in both repos with a drift check, so CUDA++ never pins omega. Still to settle with L395: the GPU files under `src/runtime/` (`rx_resident_gpu.*` and its tests) and `omega_numeric_*_gb10.*`. `groups.tsv` files a few of these under cudapp only so the loop shows. | L395 |
| C2 | none: `mk/rx_compose_lib.mk` only names `mk/allen.mk` and `mk/polyglot.mk` in a comment. `graph.sh` now skips comment lines in build files, and polyglot and allen fall out of the loop | nothing to cut |
| C3 | language uses `omega_numeric.h` (2), runtime uses `omega_matvec.h` (4), numeric uses the language (12) | split the number semantics header (stays) from the realizations (CPU and GPU), then numeric can leave as its own repo | this lane, after C1 |
| C4 | `tests/algebra/ma_digital_v1_gate.c` includes `src/turing/select.h` (1) | that gate is a Turing gate; it moves with turing | this lane, with the programs cut |

## 4. Order of the cuts (lowest risk first)

1. **Evidence shelf.** The biggest cut by file count (11,078 of 14,258 files), no code moves. Omega's history is never
   rewritten, so a full clone stays the same size; what shrinks is the checked-out tree. The gates that read a
   past receipt keep reading it: those files stay in omega (`tools/modules/graph.sh` lists the 78 evidence
   paths that code, build files or scripts name). Gates keep writing new receipts into `evidence/` as today.
   `evidence.lock` pins the shelf commit so every old receipt stays addressable as shelf + commit + path.
2. **Crypto**, with the dedupe: measure this copy against aienos `native/crypto` (L339 sped it up), keep the
   faster one, prove identical outputs on the RFC/FIPS vectors, and both repos pin the one module.
3. **Research shelf**: `research/m15`, `research/r14-failure-modes`, `research/dirac-oracle` (nothing builds them). `calibration/` is not a shelf: `mk/turing_exp001_*.mk` build its scripts and `tests/turing` read its experiments, so it moves with turing in the programs cut. `research/atemporal` (404 files) is built by `mk/at0.mk` and `mk/at1.mk`, so it moves with the programs too.
4. **Host**, **learn**, **programs**: each pins omega (and CUDA++ where used) by lock and carries its own
   gates. Omega does not pin them back, or the two repos would pin each other. Whoever needs them together
   (aienos, aien-sovereign-core) pins both, as ADR 0039 says the assembling repo does.
5. **Numeric**, after cut C3.

Per cut: the new repo keeps the history of its files (`git filter-repo`), gets a LICENSE and a plain README,
passes its own tests from an empty checkout, and gets a secrets and licence audit (authors, third-party
code, gitleaks over the kept history) before it goes public. Omega gets a branch that removes the files and
pins the module by lock, with its full gate set green and identical outputs.

## 5. The interface on each edge

| Edge | What crosses it |
|---|---|
| omega -> evidence shelf | `evidence.lock` (one commit); receipts read by path at that commit |
| omega -> crypto | `crypto.lock`; the C headers `sha256.h`, `omega_blake3.h`, `aienos_sig.h` and their object files |
| omega -> CUDA++ | `cudapp.lock` in omega (L395); the shared base is carried byte-identical in both repos with a drift check, so CUDA++ does not pin omega |
| host -> omega | `omega.lock`; `rx_caproot.h` (the host capability root) and the language core headers |
| learn -> omega, CUDA++ | `omega.lock`, `cudapp.lock`; `omega_numeric.h`, the GPU library headers |
| programs -> omega, CUDA++ | `omega.lock`, `cudapp.lock`; the language core, runtime and numeric headers |

## 6. Progress

The running table (module, repo, commit, gates, status) is kept in the L396 handoff and copied here when
each cut lands.
