# Omega `.om`: what is done, what completion still needs

Status: 2026-10-23. This file is the audit trail for the objective "complete and evolve the `.om`
language". It separates work that is implemented and gated from work that is blocked on a decision or
on an external store. It is not a claim that the language is complete.

Evidence lives in `seed/README.md` (rungs), `seed/omc/gate*.sh`, `seed/host/check.sh`, and
`evidence/SEED-0/`.

## 1. Implemented and gated

| Area | What exists | Gate / evidence |
|---|---|---|
| Core language | functions, parameters (scalar, view/edit arrays, structs), if/else, while/bound, for, break, continue, ret, trap, note, prefix values, grouping | gates 6-11, gate-ir; corpus |
| Types | bool, u8..u64, i8..i64, f32/f64, arrays with explicit length, structs of 8-byte cells | gate8, gate9, gate10, gate6 |
| Types rules | no implicit conversion; typed literals; range checks; signed traps; unsigned wrap; shifts; division/remainder; float IEEE semantics | gate14 (Wolfram-derived expectations) |
| Memory model | stack arrays (new), globals (pools), structs, view/edit, no free, no pointers, checked indices | gates 6-12, host |
| Constants | integer and float literals; expressions, calls, array lengths, struct field lengths, trap codes, loop bounds, for counts; integer folding in IR | gate16, gate-ir |
| Layout | sizeof/offsetof over 8-byte cells; scalar sizes | gate10 |
| Host words | file/path/metadata, links, permissions/ownership, access, identity, process groups, time, sleep, sockets, poll, fork/exec/wait, pipes, dup, argv/env, random, chdir, fchdir, cwd, truncate/ftrunc, chmod/chown, stat/lstat, memfd, fdatasync, pread/pwrite, getsid/syncfs/shutdown | seed/host/check.sh |
| Diagnostics | 23 named refusal codes with one minimal case each | gate15 |
| Robustness | 600 deterministic fuzz programs; capacity boundaries; 300-program differential fuzz against the pinned compiler; 30 randomized two-function IR units; keyword generator parity | gate12, gate13, gate17, gate18, gate-ir |
| IR mode | OSC IR v1 scalar subset: up to 32 functions, calls to earlier functions, integer constants folded, break; arrays/structs/floats out of scope | gate-ir (10/10 dev, 9 call units, 3 break units, 1 shift unit, 1 bool unit, 1 cast unit, 30 fuzz units, 26 refusals) |

Compiler: 7,208 lines, 251 functions, sha256
`ef19aa993d210ad20070ef99bce1e2ef02921d9541d7a443f973072c4d6fa6a3` (119,643 bytes).

Benchmark parity: 103 authored `.om` sources and 53 build-plan targets compile to identical bytes
and refusals as the pinned compiler `e5d60db8…` (rung R15; rechecked after every keyword).

## 2. Open compiler work

IR mode is complete for the **scalar** OSC-1 subset: scalar values, up to 32 functions, calls to
earlier functions with full 63-byte names, integer constants (values, trap codes, loop bounds, for
counts), and break. `gate-ir` pins this against the C oracle and checker2. No self-contained compiler
feature remains for that subset.

**Spec-blocked / model-divergent target extensions.**

1. **IR arrays.** OSC-1 arrays carry explicit `free`; the `.om` language has no `free` (decision 24)
   and returns arrays automatically. The C oracle therefore cannot validate an IR lowering of `.om`
   arrays, and IR mode refuses `new`/`get`/`put`. Needs an ownership model that both sides share.

OSC-1 also explicitly excludes structs and strings, and its scalar tags are integer-only; `omegese`
cannot emit them, so there is no reference unit to verify against.

1. **IR structs.** Version-2 struct records exist, but allocation, field ops and identity need the
   OSC-1 struct lowering spec.
3. **IR floats.** Float constants cannot be folded until the IR type system carries f32/f64.

**Decision-bound language/ecosystem work.**

4. **Generic byte/string utilities.** Array parameters carry an exact length; a standard library
   needs open arrays or a max-length convention (language decision).
5. **`sync` naming.** The benchmark corpus uses `sync` as an identifier four times; any new keyword
   must pass the corpus collision audit (rung R13).

## 3. Blocked on a decision or an external store

| FR / decision | Gap | Why it is blocked |
|---|---|---|
| FR-1 file I/O | complete for the exposed set (open/read/write/seek/stat/link/chmod/chown/truncate/memfd) | no blocker for this subset |
| FR-2 packed structs | structs are 8-byte cells; packed u8/u16/u32/u64 fields are not expressible | needs a layout syntax and alignment rule (decision) |
| FR-3 imports by digest | no `import`; units concatenate | requires the OSC content-addressed unit store (external) |
| FR-4 floats | implemented end to end for AArch64 (types, literals, arithmetic, arrays, pools, structs) | IR floats are spec-blocked: OSC-1 scalar tags are integer-only |
| FR-5 mmap/ioctl | absent | the language exposes no address values; needs an address/pointer escape decision |
| FR-6 threads/atomics | absent | decision 37 (disjoint threads) has no chosen design |
| FR-7 shared objects/C ABI | absent | needs an ABI contract (calling convention, symbols, relocation) |
| FR-8 kernel words | absent | needs a target kernel interface, not a compiler change |
| FR-9 x86-64 | absent | second backend; no decision to fund it |
| decision 24 | no heap, no free | Drake's rule; any allocation model would reverse it |
| decision 37 | no shared-state threads | design decision pending |

## 4. Next concrete step

No self-contained compiler feature remains for the supported scalar subset. Next work requires a
decision (FR-2/3/5/6/7/8/9, decisions 24/37) or a shared target-extension model (IR arrays,
structs, floats); host-surface polish is optional.
