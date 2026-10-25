# OM-2: from compact source to a check to machine code

This is the working tutorial and the guarantee matrix for the OM-2 profile of Omegese. It is not a
specification: `spec/omegese-om2.md` and `spec/omegese-om2-errata.md` are normative, and
`docs/one_layer/OMEGESE.md` §9.13 says what `omc` implements of them. Where this document disagrees
with those, they win, and this document is stale.

Estate at the time of writing: OM-2 is on the compiler stack `om2/compiler-s4` @ `f6a6492fd`
(PRs #750, #753, #754, #764) and the SHA-256 cutover `om2/sha-cutover` (PR #776), all **draft and
unmerged**. `main` compiles OM-1 only. Nothing here is hardware-qualified: every result is HOST on
Linux AArch64.

## 1. Pins

| Item | Value |
|---|---|
| Compiler source | `seed/omc/omc.om` on `om2/compiler-s4` @ `f6a6492fd` |
| Compiler binary | `seed/out/omc`, sha256 `499236398aabc652e2dae27fe4bdc4c5db3d3726314356e0dbef23e3345803ec` |
| Contract | `spec/omegese-om2.md` sha256 `245bb74e…0881`, errata E1–E18 |
| Qualification | `om2/qualification`, records in `candidates/*/RESULTS.txt` (PR #752) |
| SHA-256 implementation | `om/sha256/sha256-om2.om`, sha256 `dbf002dd…94bb` |
| SHA-256 artifact | 21,340 bytes, 5,335 words, sha256 `27775fb2…570b` |
| Bridge | `aien-dev/omega-bridge` `feat/slice-abi` @ `a8959fb1`, ABI v2 |
| Baseline for every comparison | `main` @ `acb3f15cd` |

## 2. Tutorial

### Step 1 — opt in, one line

```
note a first OM-2 unit
om2
fn main to int
ret 0
end
```

Leading blank lines and `note` lines are skipped; the first line that is not one of those decides the
profile. `om2`, `om2 diag` or `om2 fmt` makes the unit OM-2. Any other line leaves it OM-1, where the
OM-2 words are not reserved and `rotr` is simply an unknown word (error 3). Nothing else about a unit's
shape changes: one statement per line, prefix operators, `end` blocks.

### Step 2 — let the compiler hold the checks

```
om2
layout blk 8
field lo uint at 0 little
field hi uint at 4 little
end
abstract v32 ulong
holds v32 dup32
note the erasing constructor: the body must be exactly this line
fn v32mk x uint to v32
ret seal v32 or shl ulong x 32 ulong x
end
fn stamp c edit blk to uint
put c lo 0x01020304
put c hi 0x05060708
ret or get c lo shl get c hi 16
end
```

Three things are now checked by `omc` rather than at run time or not at all:

- **External layout.** `layout blk 8` fixes a 8-byte image and where each field starts; a field past
  `SIZE`, or a byte shared with an earlier field, is error 29 at that field line. Access goes through a
  `view`/`edit` parameter, so the offsets and the byte order are the compiler's, not a comment's.
- **Constructor discipline.** No `ulong` becomes a `v32` except through `seal` inside `v32mk`. A `seal`
  anywhere else is error 28. `holds v32 dup32` says every value keeps `high32 == low32`; the run-time
  check is emitted unless the constructor body is exactly the line above, in which case it is erased
  (`om2 diag` prints `omc-check 1 erased line=N rule=dup32`). Erasure is a claim the compiler proves from
  an exact body shape, so it cannot drift silently: change the body and the check comes back.
- **Definite writes.** `fn seed s edit uint 8 fresh to uint` declares scratch with undefined incoming
  elements. Reading an element that is not definitely written is error 27 on that line.

### Step 3 — compact rounds

```
om2
fn rounds s edit uint 8 fresh to uint
for i uint 8 unroll
put s i add rotr 0x9e3779b9 i 7
end
ret get s 3
end
```

`unroll` emits the body eight times with `i` constant, so the loop branch and counter disappear; `rotr`
with a number amount becomes one `ROR`. Removing `unroll` changes size and speed, never behaviour. The
same is true for `inline`. Both are bounded by contract, not by hope: 64 copies per loop, 4 inline
levels, 65536 emitted words and 65536 expansion fuel per function, 16 registers (errors 14 and 12).

Folding happens only inside an unrolled or inlined body, and never for an operation that would trap. A
**number** index into a sized array (up to 4095) becomes an immediate offset with no run-time check;
every other index keeps trap 3. Constants do not fold through parameters.

### Step 4 — one view, one slice, no wrapping arithmetic

```
om2
fn fold a view uint * to uint
x = new uint 1 0
put x 0 uint len a
for i uint uint len a
put x 0 add get x 0 rotr get a i 5
end
ret get x 0
end
```

`a view uint *` takes any length and `len a` is its run-time count. Every index into it is checked at
run time, including a number, and is never eliminated.

`part A o k` slices only as a call argument for an unsized parameter, and it checks `o <= len A` then
`k <= len A - o` — the wrapped `o + k` is never formed. That matters at the edge: with `len A = 10`,
`o = 2^64-2`, `k = 4`, a wrapped sum test would pass; OM-2 traps 3.


A complete, compiled example of steps 2, 3 and 4 together is `examples/om2-tour.om` (sha256
`856c541d…315f`). Observed with the pinned compiler: compile exit 0, run exit 0; changing one constant
inside `stamp` gives exit 11; `holds v32 dup31` gives exit 28; and with `om2 diag` the same unit emits
15 `omc-site` records plus one `omc-check 1 erased line=11 rule=dup32` — the erased constructor check,
proved from the exact body shape rather than asserted.

### Step 5 — compile, run, refuse

```sh
OMC=seed/out/omc                     # check its sha256 against section 1 first
"$OMC" < prog.om > prog 2> prog.err; echo "compile rc=$?"; cat prog.err
chmod +x prog && ./prog; echo "run rc=$?"
```

Program exit status is `main`'s value mod 256; a runtime trap exits `100 + code`. Never run the output of
a non-zero compile — a refusal may still leave bytes on stdout.

Refusals observed with the pinned compiler (`omc: line N: error K`, exit `K`):

| Source | Observed |
|---|---|
| `x = uint 0x100000000` | exit 8 (number out of range) |
| `x = uint 0X10` | exit 6 (malformed number) |
| `part a 0 2` as a statement | exit 9 (word in a disallowed place) |
| `rotr 1 1` in a unit without `om2` | exit 3 (unknown word) |
| `om2` twice | exit 16 (syntax) |
| `holds v32 dup31` | exit 28 (`obligation=ctor`) |
| a layout field past `SIZE` | exit 29 (`obligation=layout`) |
| a `get` of an unwritten `fresh` element | exit 27 (`obligation=init`) |

With `om2 diag`, stderr also carries `omc-diag 1 code=… sev=… line=… col=… span=A-B expect=… found=…
obligation=… profile=OM-2`, one `omc-site 1 line=N iter=k` per run-time trap site, and one
`omc-check 1 kept|erased line=N rule=dup32|-` per `seal`.

### Step 6 — a unit with no `main` is called through the bridge

A unit without `fn main` compiles to bare words and is not runnable on its own. `export fn NAME ...`
marks the entries: with any export, words `0..E-1` branch to the exports in source order, `E ≤ 32`, and
the other functions are private. `NAME out T N` declares a caller-owned buffer that the binding copies
out after status 0. `aien-dev/omega-bridge` owns the ABI and refuses what it cannot convert; the
`sha256_*` compatibility wrappers in the generated binding are out-of-line so that a freestanding object
including only the declaration header still needs nothing from libc beyond `mem*`.

### Step 7 — the whole thing, on SHA-256

`om/sha256/sha256-om2.om` is 344 lines, 72 of them the K/IV tables, replacing a 4,096-byte C adapter
plus an OM-1 unit. Measured on the same machine, interleaved, protocol fixed before the run
(`spec/omegese-om2-measure.md`):

| Workload | `acb3f15c` (OM-1 + C adapter) | OM-2 cutover | ratio |
|---|---|---|---|
| 1 MiB, one call | 152.5 MB/s | 210.2 MB/s | 0.726 |
| 1 MiB in 1-byte updates | 14.3 MB/s | 32.8 MB/s | 0.436 |
| 55 bytes | 496 ns | 560 ns | 1.129 (declared REGRESSION) |
| 64 bytes | 896 ns | 816 ns | 0.911 |

## 3. Guarantee matrix

Read each row as: what the compiler proves, what it checks at run time, what nothing establishes. The
class column is the strongest claim the current evidence supports; `MEASURED` numbers are HOST AArch64
only.

### Functional

| Claim | Status | Evidence |
|---|---|---|
| OM-2 SHA-256 agrees with the historical C on 2,000 cases | SUPPORTED | `tests/gates/run.sh om-units`: `SHA256-PARITY PASS cases=2000 diff=0` |
| Agrees with independent oracles (`sha256sum` 9.4, OpenSSL 3.0.13 hashlib) | SUPPORTED | 158 fixture vectors, 3,200 seeded random messages, 6 digests cross-checked in the measurement run |
| Every OM-1 unit keeps its meaning | SUPPORTED | C2 manifest 5,876/5,876 byte-identical outputs or identical errors, per commit |
| A wrong rotate count, K, IV, schedule index, pad byte, length or count is caught | SUPPORTED | 17/17 unit mutants killed, one class (a 32-bit truncated count) caught only by the ≥2^32-byte receipt |
| The compiler itself is consistent (fixpoint) | SUPPORTED, not diverse compilation | birth `om4(omc) = omc = omc(omc)` at every slice commit |
| Behaviour of arbitrary OM-2 programs | NOT ESTABLISHED | fixtures and mutants only; no proof, no exhaustive coverage |

### Bounds

| Claim | Status | Evidence |
|---|---|---|
| Every index into an unsized view is checked, numbers included | SUPPORTED | `v6-idx`, `v6-write`, and the `view-nocheck` mutant killed |
| Constant indices into sized arrays are checked at compile time | SUPPORTED | error 21 fixtures; up to 4095 lowered to an immediate offset |
| `part` checks `o <= len A`, then `k <= len A - o`, `o` before `k` | SUPPORTED | `v6-part-wrap`, `v6-part-tail`, `v6-part-o`, `v6-part-order`; three independent mutants killed |
| Host I/O slices are checked against the count | SUPPORTED | existing OM-1 rules, unchanged |
| A trap that is unreachable in practice | NOT ESTABLISHED | the model is "every check that is not proven stays" |

### Initialization

| Claim | Status | Evidence |
|---|---|---|
| A `fresh` element read before a definite write is refused | SUPPORTED | error 27 fixtures, 5 fresh mutants killed, join-intersection rules gated |
| A layout-typed context is fully written before it is read | SUPPORTED for the SHA unit | `f5-part`, and the mutant that leaves `w[15]` unwritten is error 27 at that line |
| `fresh` analysis across arbitrary control flow | PARTIAL BY DESIGN | if-without-else, `while` and rolled `for` do not advance the set; that is conservative, never optimistic |
| Stack exhaustion from deep recursion | UNSUPPORTED as a status | a fault, not a returned status (trap 6 covers the local-array budget only) |

### Module and FFI boundaries

| Claim | Status | Evidence |
|---|---|---|
| Units compose by concatenation; there is no import | SUPPORTED (a limit, not a feature) | OM-1 rule, unchanged in OM-2 (FR-3 open) |
| ABI v2: `omega2_<export>` symbols, `omega2_ident_<unit>()`, `out` through a caller buffer | SUPPORTED | identity records (E1), 19 status-32 refusals that leave state untouched, six-language v1 refusal |
| A freestanding object needs no new libc surface | SUPPORTED | `rx_cortex.o` undefined set identical to `main`'s; `RX_CX_FS_ALLOWED` unchanged (E18) |
| Calling convention drift between compiler and bridge | DETECTED, not prevented | the bridge refuses unknown slots/statuses; a mismatch surfaces as status 32 or a link error |
| Packed structs, imports, shared objects, heap | UNSUPPORTED at this pin | FR-2, FR-3, FR-7, decision 24 |

### Timing and leakage

| Claim | Status | Evidence |
|---|---|---|
| The OM-2 SHA's `init` and `hash` are clean under the static scanner | SUPPORTED | `pub:` scan PASS |
| The OM-2 SHA's `update` and `final` are clean under the static scanner | **NOT_RUN** | 83 and 3 MANUAL branches, 67 and 1 data-indexed accesses, all derived from the byte-count field the scanner cannot treat as public |
| Equal-length messages take the same instruction path | MEASURED on 28 pairs | single-step traces identical; negative controls detected |
| Constant-time behaviour in general | **NO CLAIM** | trace equivalence only (E12); speculative constant-time is UNSUPPORTED |
| Performance | MEASURED, see section 2 step 7 | bulk 1.38× faster; 1–55-byte inputs 6.5–12.9% slower under the predeclared policy |

### Effects and authority

| Claim | Status |
|---|---|
| A guide, a model review or a passing gate grants no authority to run anything | RULE |
| Host execution is not hardware qualification, and no GPU or device claim is made here | RULE |
| Merging this stack moves `main`'s compiler pin for every consumer | RULE: the stack lands as one unit or not at all |

## 4. What to do when the compiler refuses

1. Read the code: `27` is initialization, `28` is constructor discipline, `29` is layout, `14` is
   growth/fuel and also the 33rd export, `12` is registers, `9` is a word in the wrong place.
2. Reproduce it as small as possible in the same profile, and keep the smallest failing unit.
3. Decide which side is wrong: your program, a documented limit (OMEGESE §9.13), or the compiler. A
   limit is not a bug, and an unimplemented word must be reported as such — never worked around in C.
4. Fix one thing, rerun the unit and a negative control, and record the pin you used.
