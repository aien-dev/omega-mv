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
| Host words | file/path/metadata, links, permissions/ownership, access, identity, process groups, time, sleep, sockets, poll, fork/exec/wait, pipes, dup, argv/env, random, chdir, fchdir, cwd, truncate/ftrunc, chmod/chown, stat/lstat, memfd, fdatasync, pread/pwrite | seed/host/check.sh |
| Diagnostics | 23 named refusal codes with one minimal case each | gate15 |
| Robustness | 600 deterministic fuzz programs; capacity boundaries | gate12, gate13 |
| IR mode | OSC IR v1, up to 32 functions, calls to earlier functions, integer constants folded (values, trap, bound, for); refuses what it cannot lower | gate-ir (10/10 dev programs, 4 multi-call units, 13 refusals) |

Compiler: 7,073 lines, 247 functions, sha256
`7c51a738aaa034e0897157b03c9ae87c48ab1810a69afe2fa49ef02f2b9a735d` (117,737 bytes).

Benchmark parity: 103 authored `.om` sources and 53 build-plan targets compile to identical bytes
and refusals as the pinned compiler `e5d60db8…` (rung R15; rechecked after every keyword).

## 2. Open language/compiler work (no external decision needed)

1. **IR structs (spec-blocked).** OSC-1 explicitly excludes structs. The format's version-2 struct
   records have no lowering reference, and the C `omegese` refuses `struct`, so an emitter cannot be
   verified against an oracle. Needs the OSC-1 struct lowering spec (allocation, field ops, identity).
2. **IR floats (spec-blocked).** OSC-1 scalar tags are integers only; IR refuses float words. Float
   constants cannot be folded until the IR type system carries f32/f64.
3. **IR call names.** Calls are earlier-only by design (checker2 enforces it), scalar, at most six
   arguments. The emitter currently matches call names by their first 8 bytes; a split
   name-comparison helper can lift this to the 63-character format limit.
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

Lift the 8-character IR call-name limit with a split name-comparison helper (checker2 accepts names
up to 63 bytes). Beyond that, no self-contained kernel-language feature remains: the next work is
decision-bound (FR-2/3/5/6/7/8/9, decisions 24/37) or spec-bound (OSC-1 struct/float lowering).
