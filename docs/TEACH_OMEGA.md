# One small Omega program: source, executable, and the identity boundary

This lesson follows the existing seeded `.om` forward-call fixture. It also
explains where the older semantic-identity machinery lives **without pretending
that the two pipelines are integrated**. It is a bounded contribution to
[#619](https://github.com/aien-dev/omega-mv/issues/619), not its closure.

## What problem is being solved?

The [2026-09-29 decision record](adr/OMEGA-SYSTEMS-CORE-0000.md)
sets the goal: replace convention-dependent C/assembly with explicit machine
semantics, reproducible compilation, and separate semantic, runtime, wire and
device layouts. The later seed work makes compiler ancestry inspectable: explicit
instruction words build small translators, then a compiler written in Omega.
See the existing [seed-birth walkthrough](https://github.com/aien-dev/omega-mv/blob/b30ba6cf729eb9295c7f8883cfff0519b90a63e1/docs/SEED_BIRTH.md)
and its [recorded replay](https://github.com/aien-dev/omega-mv/blob/b30ba6cf729eb9295c7f8883cfff0519b90a63e1/evidence/seed-birth-20261022/README.md).
Those are development-branch documents, not files already integrated on `main`.

Published Arm instruction and ELF formats are upstream specifications, not
Omega inventions. The seed notes attribute them. Existing Linux, the shell and
host utilities remain dependencies. [CONTRIBUTORS](../CONTRIBUTORS.md) attributes
project implementation to AI models orchestrated by Drake Stapleton; this lesson
does not invent a personal account of who typed the compiler.

## Pin and prerequisites

Use the teaching pin `29b12760f4c962606411d6bf25032aedef063301`, the R5
compiler described by the existing replay above. This is **not the newest compiler,
a production recommendation, or a combined profile**. Later native-host and
device work has separate review and qualification, including
[PR #678](https://github.com/aien-dev/omega-mv/pull/678) and
[PR #676](https://github.com/aien-dev/omega-mv/pull/676).
Do not substitute one profile's compiler hash or evidence for another's.

Execution requires **AArch64 Linux**, Git, a POSIX shell, and ordinary utilities
used by the bootstrap (`printf`, `tr`, `cat`, `sed`, GNU-compatible `stat`, `cmp`,
`chmod`, `mkdir`, `cp`, `wc`, `cut`, `grep`, `sha256sum`). No model, GPU, CUDA
installation, physics checkout, or package download is part of this example.
Cloning needs network access; after checkout the recipe is offline. An x86 host
can inspect source but cannot natively execute these AArch64 binaries.

Use a disposable checkout: the bootstrap writes `build/seed/birth`, `seed/out`,
and rung-4 generated files, including a measured-size field in `om4.s`.
Inspect the [bootstrap](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/omega-from-seed.sh)
and [rung-4 builder](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung4/mk4.sh)
before executing them. Never run historical receipt-writing gates in a checkout
whose evidence you intend to preserve.

## Follow the source

The unchanged input is
[`seed/rung4/tests/r5b_fwd.om`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung4/tests/r5b_fwd.om):

```text
fn main to int
ret later
end
fn later to int
ret 7
end
```

`main` returns the result of `later`, even though its declaration follows the
call. The expected observable result is **process exit 7**, not printed text.
A nonzero program result is not automatically a compiler/test failure.

In the pinned
[`seed/omc/omc.om`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/omc/omc.om),
follow these function names:

| Stage | Implementation and intermediate representation |
| --- | --- |
| Read source | `main` reads stdin into bounded `inp`; `tok` scans tokens. |
| Discover/check names | `prescan` and `namescan` collect declarations; `stmt`, `s_name`, `s_assign` and expression readers enforce this subset's rules. This is not a claim of the full OSC contract system. |
| Emit and resolve | Statement/expression handlers emit instruction words into `out`; `resolve` fills call offsets after addresses are known. |
| Make the executable | `eof_prog` constructs the entry/exit sequence; `elfhdr` writes the ELF64 little-endian AArch64 header; `writeout` sends the image to stdout. |
| Run | Linux loads that ELF; the generated entry calls `main` and passes its result to the Linux exit syscall. |

There is no C compilation or linking step in this construction path. The
bootstrap's fixed-point comparisons establish self-consistency of the tested
compiler builds, not general semantic correctness or diverse-compilation trust.

## Replay on a suitable CPU host

These are **expected results derived from source and the linked historical
replay**, not a claim that this lesson's author reran them. Execute each block
in order in the same shell. Each block fails closed on an unexpected result.

```sh
set -eu
git clone https://github.com/aien-dev/omega-mv.git omega-teaching-replay
cd omega-teaching-replay
git checkout --detach 29b12760f4c962606411d6bf25032aedef063301
test "$(git rev-parse HEAD)" = 29b12760f4c962606411d6bf25032aedef063301
sh seed/omega-from-seed.sh
printf '%s  %s\n' \
  191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9 \
  seed/out/omc | sha256sum -c -
mkdir build/teaching
seed/out/omc < seed/rung4/tests/r5b_fwd.om > build/teaching/fwd
chmod +x build/teaching/fwd
rc=0
build/teaching/fwd > build/teaching/fwd.stdout || rc=$?
test "$rc" -eq 7
test ! -s build/teaching/fwd.stdout
printf 'forward-call exit=%s\n' "$rc"
```

Expected: bootstrap exits 0 and prints `RC=0`; hash check prints
`seed/out/omc: OK`; compilation exits 0; program exits 7 with no stdout;
the last line is `forward-call exit=7` and the block exits 0.

### A meaning-preserving change

Rename the internal function consistently; keep the externally required `main`.
This changes source bytes but not the intended call/result. Byte equality is a
stronger, implementation-specific expectation for this exact rename, **not a
universal test of semantic equivalence** and not a `SEMANTIC_ID` computation.

```sh
sed 's/later/after/g' seed/rung4/tests/r5b_fwd.om > build/teaching/renamed.om
seed/out/omc < build/teaching/renamed.om > build/teaching/renamed
cmp build/teaching/fwd build/teaching/renamed
chmod +x build/teaching/renamed
rc=0
build/teaching/renamed > build/teaching/renamed.stdout || rc=$?
test "$rc" -eq 7
test ! -s build/teaching/renamed.stdout
printf 'renamed exit=%s\n' "$rc"
```

Expected: compiler and `cmp` exit 0, program exit 7, no program stdout,
`renamed exit=7`. The new rename experiment is **NOT_RUN** here; if comparison
fails, preserve its output and report FAIL rather than changing expected bytes.

### A safe refusal already in the bootstrap

[`r5b_assign.om`](https://github.com/aien-dev/omega-mv/blob/29b12760f4c962606411d6bf25032aedef063301/seed/rung4/tests/r5b_assign.om)
tries `later = 2` where `later` is a function. The names pass makes that an
invalid assignment, rather than silently treating the function name as a variable.

```sh
rc=0
seed/out/omc < seed/rung4/tests/r5b_assign.om \
  > build/teaching/refused.bin 2> build/teaching/refused.stderr || rc=$?
test "$rc" -eq 7
grep -q 'error 7' build/teaching/refused.stderr
printf 'invalid-assignment compiler-exit=%s\n' "$rc"
```

Expected: compiler exit 7, diagnostic containing `error 7`, final line
`invalid-assignment compiler-exit=7`. **Do not execute refused.bin.** The
bootstrap already asserts this exact exit and diagnostic. The program result 7
above and compiler refusal 7 here are different events from different processes.

## Where semantic identity and receipts fit: and do not yet fit

The seeded fixture above does **not** travel through the legacy C
`OmegaProgram` pipeline. No canonical semantic encoding, `SEMANTIC_ID`,
`REALIZATION_ID`, or signed qualification receipt is emitted by this recipe.
A SHA-256 of the executable is an artifact fingerprint, not semantic identity.
For this lesson, the missing native integration is **MISSING_IMPLEMENTATION**,
not an invitation to revive the legacy C path as the permanent foundation.

The existing legacy implementation supplies a precise comparison and source map:

| Concept | Existing source, format and test |
| --- | --- |
| Parse/check a restricted unary function | `omega_language_parse_line` and `omega_language_lower_program`, exercised by `lower_fn` in [test_program_id.c](../tests/program/test_program_id.c). This is a different source grammar from `.om`. |
| Bind meaning, not spelling/cost | [program identity v2](../spec/program-identity.md): `omega_program_compute_id` hashes a domain tag, canonical body-root id, input/output type ids and pre/postcondition ids. [omega_program.c](../src/omega_program.c) and [omega_canonical.c](../src/omega_canonical.c) implement it. |
| Inspect intermediate bytes | [canonical object encoding](../spec/canonical-encoding.md) and [program IR layout](../src/omega_program_ir.h); [IR tests](../tests/test_omega_program_ir.c) exercise round trips and refusals. These are distinct encodings, not the emitted ELF. |
| Realize and compare | [program realization V0](../spec/program-realization.md) links `omega_program_realize_check`, scheduling, emission and `omega_realization_verify_program`; unsupported bodies and stale ids fail closed. Native comparison requires AArch64. |
| Receipt binding | [omega_receipt.h](../src/omega_receipt.h) defines receipt canonicalization/identity; an execution log alone does not satisfy that schema or establish qualification. |

For example, the identity test compares `fn f(x: u64) -> u64 { x + 1 }`
with changed names/representation and separately with `x + 2`. Equality is
about the supported canonical body and contract, **not every mathematically
equivalent arbitrary program**. The documented v1 failure was omission of the
body from the hash, allowing different programs to share an id; v2 fixes that.
The realization design also records the earlier error of emitting `3x - 2`
regardless of the requested program. These are attributed design records, not
newly reproduced failures or claims that all compiler defects are solved.

## Evidence and remaining work

- Historical AArch64 bootstrap replay: linked above, exact R5 pin and compiler
  fingerprint; not a fresh run of this whole lesson.
- This documentation pass: source/path/snippet and shell-syntax inspection only.
  Execution, the new rename comparison, and repository judges: **NOT_RUN**.
- Full #619 acceptance still needs an independent fresh CPU replay with revision,
  OS/architecture, commands, stdout/stderr and exit statuses, plus an honestly
  scoped native identity/receipt story. Preserve failures and never rewrite old
  receipts to make the example pass.
- GB10/device/model qualification is outside this CPU lesson. It requires its own
  exact compiler/profile, hardware, operator scheduling and evidence; a host pass
  cannot establish it. No hardware extension is necessary to learn this example.
