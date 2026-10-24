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
| Host words | file/path/metadata, links, permissions/ownership, access, identity, process groups, time, sleep, sockets, poll, fork/exec/wait, pipes, dup, argv/env, random, chdir, fchdir, cwd, truncate/ftrunc, chmod/chown, stat/lstat, memfd, fdatasync | seed/host/check.sh |
| Diagnostics | 23 named refusal codes with one minimal case each | gate15 |
| Robustness | 600 deterministic fuzz programs; capacity boundaries | gate12, gate13 |
| IR mode | OSC IR v1, up to 32 functions, calls to earlier functions, integer constants folded (values, trap, bound, for); refuses what it cannot lower | gate-ir (10/10 dev programs, 4 multi-call units, 13 refusals) |

Compiler: 7,039 lines, 246 functions, sha256
`0eec74110bb3580dcd0aaff0f963bdb4010183e3e89cdf14e8a2a54eef89f01a` (117,218 bytes).

Benchmark parity: 103 authored `.om` sources and 53 build-plan targets compile to identical bytes
and refusals as the pinned compiler `e5d60db8…` (rung R15; rechecked after every keyword).

## 2. Open language/compiler work (no external decision needed)

1. **IR structs.** Version 2 records exist in the format (names, fields, cells); the emitter refuses
   `struct`. Needs FLOAD/FSTORE and struct-typed references with the cell layout already used by the
   AArch64 path.
2. **IR float constants.** Float constants remain refused in IR (error 9); folding them needs the
   IR literal path for f32/f64.
3. **IR call limits.** Calls are earlier-only by design, scalar, at most six arguments, and call
   names are limited to 8 characters in IR mode. Forward-call prescan would lift the last of these
   and let calls precede definitions.
4. **Generic byte/string utilities.** Array parameters carry an exact length, so one library routine
   cannot take arrays of several lengths. A standard library needs either open arrays or a
   max-length convention; that is a language decision, not a library chore.
5. **`sync`** cannot be a keyword: the benchmark corpus uses it as an identifier four times. Any
   name must pass the corpus collision audit (rung R13).

## 3. Blocked on a decision or an external store

| FR / decision | Gap | Why it is blocked |
|---|---|---|
| FR-1 file I/O | complete for the exposed set (open/read/write/seek/stat/link/chmod/chown/truncate/memfd) | no blocker for this subset |
| FR-2 packed structs | structs are 8-byte cells; packed u8/u16/u32/u64 fields are not expressible | needs a layout syntax and alignment rule (decision) |
| FR-3 imports by digest | no `import`; units concatenate | requires the OSC content-addressed unit store (external) |
| FR-4 floats | implemented end to end (types, literals, arithmetic, arrays, pools, structs) | none for AArch64; IR float folding open |
| FR-5 mmap/ioctl | absent | the language exposes no address values; needs an address/pointer escape decision |
| FR-6 threads/atomics | absent | decision 37 (disjoint threads) has no chosen design |
| FR-7 shared objects/C ABI | absent | needs an ABI contract (calling convention, symbols, relocation) |
| FR-8 kernel words | absent | needs a target kernel interface, not a compiler change |
| FR-9 x86-64 | absent | second backend; no decision to fund it |
| decision 24 | no heap, no free | Drake's rule; any allocation model would reverse it |
| decision 37 | no shared-state threads | design decision pending |

## 4. Next concrete step

Implement IR structs against `spec/one-layer-format.md` §3.2/§3.4 (struct records, FLOAD/FSTORE,
struct-typed references), keeping the current one-function and multi-call comparisons byte-identical
and adding a struct unit the C oracle and checker2 accept. That is the last self-contained IR
feature that needs no operator decision.
