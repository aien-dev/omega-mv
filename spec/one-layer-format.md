# Specification: OSC binary IR, canonical format and strict reader

```text
Document ID:   SPEC-ONE-LAYER-FORMAT
Issue:         aien-spark/omega#444 (one-layer plan, stage 2 "Open the binary front door")
Status:        IMPLEMENTED for format versions 1..5 (reader + writer); version 6 items OPEN (section 9)
Code:          src/compiler/osc_ir.h (osc_ir_encode, osc_ir_decode, OscIrRefusal)
               src/compiler/osc_ir.c (encode_unit, unit_version, decode_unit)
Tests:         tests/compiler/test_osc_ir_decode.c; make test-osc-ir-decode(-quick)
Data layout:   src/compiler/osc_ir.h is normative for OscUnit; meaning: docs/osc/OSC-1-DESIGN.md
```

## 1. What this format is

A compiled OSC unit (`OscUnit`, `src/compiler/osc_ir.h`) has one canonical byte form, written by
`osc_ir_encode`. Its SHA-256 is the unit's identity (`osc_ir_digest`; docs/osc/OSC-1-DESIGN.md
section 4). Until this change the bytes could only be written; `osc_ir_decode` now reads them back
without the text front end, so a machine can hand Omega executable meaning as bytes.

## 2. Canonical law

The canonical set is exactly

    { osc_ir_encode(u) : osc_ir_validate(u) == 0 }

`osc_ir_decode(buf, len, out)` returns `OSC_IR_OK` only when `buf[0..len)` is in that set, and then
fills `out` with the unit `u` that encodes to it. Every other input is refused with one named class
(section 7) and `out` is left all zero. The reader never repairs, skips, pads, truncates or guesses.

Consequences, each checked by `tests/compiler/test_osc_ir_decode.c`:

- encode -> decode -> encode is byte-identical for every admitted input;
- an admitted input re-encodes to exactly itself (no second spelling of a unit exists);
- the decoded unit passes `osc_ir_validate`, so the interpreter and back ends accept it.

## 3. Wire layout

All integers little-endian, no padding, no alignment, no pointers.

### 3.1 Header

| bytes | field |
|---|---|
| 7 | magic `4F 53 43 31 49 52 00` ("OSC1IR\0") |
| 1 | version, 1..5 (section 4) |
| versions 2..5 only: 1 | `nstructs` 0..16, then `nstructs` struct records (3.2) |
| 2 | `nfuncs` 0..32, then `nfuncs` function records (3.3) |

Nothing may follow the last function record.

### 3.2 Struct record (versions 2..5)

| bytes | field |
|---|---|
| 1 + n | name: length n 0..63, then n bytes, no NUL |
| 1 | `nfields` 0..16 (validation requires 1..16) |
| 2 | `ncells` |
| per field: 1 + n | field name (as struct name) |
| per field: 1 | scalar tag (3.4) |
| per field: 2 | `alen` (0 = scalar field) |
| per field: 2 | `off` |

### 3.3 Function record

| bytes | field |
|---|---|
| 1 + n | name: length n 0..63, then n bytes, no NUL |
| 1 | `nparams` |
| type | return type (3.4) |
| 2 | `nvregs` 0..480, then `nvregs` types (3.4) |
| 2 | `nblocks` 0..512, then per block: 4 `first`, 4 `count` |
| 4 | `ninsns` 0..4096, then `ninsns` instructions (3.5) |
| 2 + n | `requires` text: length n 0..255, then n bytes, no NUL |
| 2 + n | `ensures` text: as `requires` |

### 3.4 Type

One byte scalar tag: 0 void, 1 bool, 2 u8, 3 u16, 4 u32, 5 u64, 6 i8, 7 i16, 8 i32, 9 i64,
10 ref, 11 bytes, 12 cells. Tags above 12 are refused. For every tag except 10 that byte is the
whole type (the decoded `ref`, `elem`, `len`, `sid` are 0). Tag 10 (ref) continues:

| bytes | field |
|---|---|
| 1 | ref kind: 0 none, 1 own, 2 shared, 3 mut (above 3 refused) |
| 1 | element scalar tag |
| 2 | `len` |
| 1, only when the element tag is 0 (void) | `sid`: struct reference to `structs[sid-1]` |

An array reference always has a value element type and `sid` 0; a struct reference always has
element void and `sid` >= 1 (`type_is_ref`, `src/compiler/osc_ir.c`). The element byte therefore
decides, without ambiguity, whether the `sid` byte is present.

### 3.5 Instruction

One opcode byte, then the opcode's fields in this order. `r` is a 2-byte register, block or callee
field: 0x0000..0x7FFF, or 0xFFFF for "none" (-1); 0x8000..0xFFFE are refused (RANGE).

| op | name | fields after the opcode |
|---|---|---|
| 1 | CONST | r dst, 8 imm |
| 2 | MOV | r dst, r a |
| 3 | BIN | 1 sub, r dst, r a, r b |
| 4 | UN | 1 sub, r dst, r a |
| 5 | CMP | 1 sub, r dst, r a, r b |
| 6 | CAST | r dst, r a |
| 7 | ALLOC | r dst, r a |
| 8 | RELEASE | r a |
| 9 | LOAD | r dst, r a, r b |
| 10 | STORE | r a, r b, r c |
| 11 | CALL | r dst, r callee, 1 nargs (0..6; above 6 refused OVERSIZED), nargs x r arg |
| 12 | TRAP | 1 imm (trap code) |
| 13 | BR | r blk_t |
| 14 | CBR | r a, r blk_t, r blk_f |
| 15 | RET | r a |
| 16 | FLOAD | r dst, r a, r b, 1 imm (field index) |
| 17 | FSTORE | r a, r b, r c, 1 imm (field index) |
| 18 | AOPEN | r dst, 1 imm (cells) |
| 19 | AALLOC | r dst, r a, r b |
| 20 | ADESTROY | r a |
| 21 | POPEN | r dst, 1 sub (element tag), 1 nargs (slots), 8 imm (generation) |
| 22 | PCLOSE | r a |
| 23 | HALLOC | r dst, r a, r b |
| 24 | HGEN | r dst, r a, r b |
| 25 | HFREE | r a, r b, r c |
| 26 | HLOAD | r dst, r a, r b, r c |
| 27 | HSTORE | r a, r b, r c, r args[0] |
| 28 | SLOAD | r dst, r a, r b |
| 29 | SSTORE | r a, r b, r c |

Opcode 0 and opcodes above 29 are refused (RANGE): their length is unknown. The numbers are pinned
by `_Static_assert`s in the test. The meaning of every field and every value range that
`osc_ir_validate` enforces (types agree, registers in range, blocks partition the instructions,
calls only to earlier functions, trap codes 1..14, and the rest) is in `osc_ir.h` and
docs/osc/OSC-1-DESIGN.md.

### 3.6 Fields the format does not carry

`OscInsn.line` (debug information; source layout never reaches identity), and per opcode every
field not listed in 3.5. The reader sets them as the front end's emitter does
(`emit`, `src/compiler/osc_lower.c`): `line` 0; `dst`, `a`, `b`, `c`, `blk_t`, `blk_f`, `callee`
-1; everything else 0; HSTORE `nargs` 1. All array entries past a count are 0. The test compares
each decoded unit with the unit the compiler produced on every carried field.

## 4. Versions and historical readers

The version byte is a function of the content (`unit_version`, `src/compiler/osc_ir.c`): 5 if any
register is a byte slice (bytes/cells), else 4 if any pool instruction (POPEN..HSTORE) occurs,
else 3 if any arena instruction (AOPEN, AALLOC, ADESTROY) occurs, else 2 if the unit has structs,
else 1. Versions 2..5 share one layout (struct table present, possibly empty); version 1 has no
struct table. Every version 1..5 is read. A version byte that is not the one the content requires
is refused (VERSION_MISMATCH), so one unit never has two encodings. Versions are only ever added;
an existing version's bytes are never reinterpreted.

## 5. Limits

Counts are checked before anything under them is read: `nstructs` <= 16, `nfields` <= 16,
`nfuncs` <= 32, `nvregs` <= 480, `nblocks` <= 512, `ninsns` <= 4096, CALL `nargs` <= 6, names
<= 63 bytes, contract texts <= 255 bytes (the `OSC_MAX_*`, `OSC_NAME_MAX`, `OSC_CLAUSE_MAX`
constants of `osc_ir.h`). Input longer than `OSC_IR_ENCODED_MAX` (2,620,251 bytes: every count at
its maximum, every instruction at the widest form, CALL with six arguments, 18 bytes) is refused
before parsing.

## 6. Identity

`identity = SHA-256(canonical bytes)`. The magic and version byte are inside the hashed bytes, so
they act as the domain tag that spec/program-identity.md section 2 requires of an identity law: a
reader that sees the bytes knows which layout produced them, and two versions can never share a
preimage. Function names and the `requires` / `ensures` texts are inside the hashed bytes, so a
change to a requirement changes the identity (tested: 514 one-character changes over the corpus,
each a new digest, each round-trips). OSC unit identity is separate from Omega program identity v2
(`omega.program.v2`, spec/program-identity.md); this change does not alter either law.

## 7. Refusal classes

`osc_ir_decode` returns the first defect met while reading front to back; the checks after parsing
run in the order listed. `osc_ir_refusal_name` gives the names.

| code | class | meaning | counterexample in the test |
|---|---|---|---|
| -1 | ARG | NULL input or output pointer | `osc_ir_decode(NULL, 8, u)` |
| -2 | MAGIC | first 7 bytes are not the magic | "OSC2IR..." |
| -3 | UNKNOWN_VERSION | version byte 0 or above 5 | versions 0 and 6 |
| -4 | TRUNCATED | input ends inside a field | empty input; last byte missing; magic only |
| -5 | OVERSIZED | input over `OSC_IR_ENCODED_MAX`, or a count or length over section 5 | 64-byte name; 481 registers; 256-byte requires text; 2,620,252-byte input |
| -6 | TRAILING | bytes after the last function | one extra byte |
| -7 | RANGE | unknown opcode, scalar tag or ref kind; a register/block/callee field 0x8000..0xFFFE | opcodes 0 and 30; tag 13; register 0x8000 |
| -8 | TEXT | a NUL byte inside a name or contract text (it would read back shorter: ambiguous) | name "f\0g" |
| -9 | INVALID | well-formed, but `osc_ir_validate` refuses the unit | u8 constant 256; register out of range |
| -10 | VERSION_MISMATCH | version byte differs from the one the content requires (section 4) | version 2 or 5 for a plain unit |
| -11 | NONCANONICAL | the unit re-encodes to different bytes | none: unreachable (below) |

NONCANONICAL is a backstop. Given the checks above it, every field read back writes back to the
same bytes, so no input reaches it; the sweeps assert it is never returned. It is reached only when
a check above it is removed: decoder mutants 1, 2, 5 and 8 (section 8) fall through to it.

## 8. Qualification

`make test-osc-ir-decode` (part of `test-compiler`, the full CI leg) and
`make test-osc-ir-decode-quick` (part of `test-compiler-quick`, run on every compiler-path change)
build `tests/compiler/test_osc_ir_decode.c` plain and with ASan/UBSan. Measured on the Spark,
2026-10-14, full mode:

- corpus: all 59 units the front end compiles from `tests/compiler/progs`, `tests/compiler/slices`,
  `src/osh`, `tests/physics0/osc` (32 v1, 7 v2, 6 v3, 3 v4, 11 v5) round-trip byte-exactly and
  equal the compiled unit on every carried field;
- 10,000 distinct valid objects (seeded variants of the corpus: constants, names, contract text,
  operators, trap codes) round-trip byte-exactly (2,000 under ASan);
- 202,675 malformed inputs (every byte of every unit up to 2,000 bytes flipped three ways, every
  truncation, trailing bytes, seeded multi-byte edits; larger units sampled at an even stride):
  177,013 refused with a named class and a zeroed unit, 25,662 decoded to a different valid unit
  that re-encodes to exactly those bytes (for example a changed constant), none admitted otherwise,
  no crash. The ASan/UBSan build runs the same number of cases (177,177 refused, 25,498 admitted;
  the random edits differ because it draws fewer valid variants first) with no sanitizer report;
- one counterexample per reachable class (section 7), built by an independent writer in the test,
  whose canonical case is byte-identical to `osc_ir_encode`;
- negative controls: 5 planted defects caught by the test's own checks, and 8 decoder mutants
  (`OSC_IR_DECODE_MUTANT` = 1 no trailing check, 2 no version check, 3 no zeroing on refusal,
  4 no validation, 5 no NUL check, 6 no register range check, 7 no total size check, 8 truncation
  read as zeros) each make the test fail.

## 9. Open (asked by #444, not in this change)

- **Numeric identifiers.** Versions 1..5 still carry function, struct and field names as text, and
  names are part of identity. A version 6 that identifies functions, structs and fields by index
  only (calls and field accesses already do) is not specified here.
- **Structured requirement objects.** `requires` / `ensures` are carried as source text; their
  checks are lowered into the instruction stream by the front end. A structured, machine-readable
  requirement object (and its place in identity) needs a design decision first.
- **Domain tag string.** Identity is versioned by the magic and version byte inside the hashed
  bytes (section 6), not by a separate `omega.osc.vN` tag string in the style of
  spec/program-identity.md. Adopting such a tag would change every existing OSC unit identity.

## 10. Seeded memory profile: IR v2 milestone, OSC wire v7

**Specification only; producer and backend qualification is separate.** The
milestone name “IR v2 memory” does not change the meaning of wire version 2,
which already carries structs. Wire versions 1..5 and their identity bytes
remain unchanged. Wire version 6 remains reserved by draft PR #525 for its
numeric-identifier/structured-contract proposal; this profile neither closes
nor supersedes that proposal. The memory extension uses version 7. A backend
that has not implemented this profile must refuse it, not ignore its regions.

### 10.1 Header and regions

All fields remain unsigned little-endian integers without wire padding:

```
magic[7] = "OSC1IR\0"
version:u8 = 7
nstructs:u8, structs[nstructs]       existing section 3.2 encoding
nregions:u16, regions[nregions]
nfuncs:u16, functions[nfuncs]        existing section 3.3 encoding
```

The struct table stays before the region table. Supporting this profile does
not imply supporting structs: a backend may explicitly refuse a nonempty
struct table. A region has the following encoding:

| Field | Encoding and rule |
|---|---|
| name | u8 length, then 1..63 non-NUL ASCII bytes |
| element | u8 scalar tag: 2..9, 13 f32, or 14 f64 |
| storage | u8 class from the table below |
| owner | u16 function index, or 65535 for module scope |
| count | u32 number of elements |
| initial length | u32 byte count |
| initial data | exactly initial-length bytes |

Region IDs are zero-based table indices, not addresses or value registers.
Table order is source declaration order; names and order participate in
identity. Names are unique within an owner scope and may not shadow a module
region in an owning function. The initial profile admits at most 1024 regions.
Mutable regions have 1..16777216 elements; an empty read-only text is allowed.
Existing native aggregate capacity limits still apply to source compilation.
Backends check each element-count multiplication, alignment and aggregate
addition for overflow before allocating or emitting memory. Source global
pools retain the native 1 GiB aggregate limit and local arrays the native
4 MiB aggregate limit. Backend input/output limits remain independently
enforced; an unsupported size is a capacity refusal, never truncation.

| Class | Meaning | Owner and initialization |
|---|---|---|
| 0 | Module-private mutable global | owner 65535; no initial data; zero-filled once per module instance |
| 1 | Function-local mutable array | owner is its function; no initial data; fresh logical storage on every invocation |
| 2 | Read-only text | owner 65535; element u8; initial length equals count; no implicit NUL |
| 3 | Reserved Workgroup | refused until initialization, synchronization and invocation ownership have a specified extension |

No other class is admitted. Function-local regions are accessible only by
their owning function. Calls remain restricted to earlier functions, so
there is no recursive or reentrant invocation within a module call.
An implementation may reuse physical storage for a function-local region
between calls, but valid Omega execution cannot observe a previous
invocation's values. Exported linear memory is not a JavaScript isolation
or zeroization boundary; a JavaScript holder can inspect physical storage.
The source initializer is evaluated once, at the `new` declaration, and fills
every element before the array is accessible. Its value is not a module data
segment. Input validation must establish that every local-region access is
dominated by initialization of that region on the current invocation.

Memory-backed scalar state uses a one-element region. Ordinary scalar
locals remain value registers; this profile does not allocate regions for
them or introduce a new source spelling for scalar globals.

For a future SPIR-V backend, class 1 maps to Function storage and class 2 to
read-only constant storage. Class 0 requires an explicitly persistent
CrossWorkgroup state ABI, or a named refusal: SPIR-V Private storage is
per-work-item and cannot implement persistence between module calls.
Class 3 is reserved rather than pretending a CPU global supplies barriers.

### 10.2 Instructions and bounds

Existing opcodes, including reference LOAD/STORE 9/10, retain their meaning.
The following instructions are appended; every operand below is u16:

| Opcode | Name | Operands |
|---|---|---|
| 30 | RLOAD | destination register, region ID, index register |
| 31 | RSTORE | region ID, index register, source register |
| 32 | BITCAST | destination register, source register |
| 33 | RINIT | region ID, initializer register |

Every operand must designate an existing region or register of the stated
kind. None of these instructions admits a sentinel operand. BITCAST and
RINIT source registers are definite-assignment uses; RLOAD and BITCAST
destinations are definitions.

RLOAD's destination and RSTORE's source have exactly the region element type.
Indices have an integer tag 2..9, never bool or float. Signed negative indices
and indices greater than or equal to count trap with Omega bounds trap 3
(native exit 103). Bounds are checked before multiplication, addition,
pointer formation or narrowing to a wasm32 address. An invalid operation
does not read or write its target element. Earlier side effects remain.
RSTORE into read-only storage is invalid IR, not a silently ignored store.
Index and source registers participate in definite-assignment validation.

There is no unchecked access mode in this profile. An explicit source bounds
test can select a nontrapping path, but it does not disable checks on the
access. The backend may eliminate a check only when it proves the same
in-bounds condition. Whole-linear-memory wasm bounds checks alone are
insufficient: crossing into the next region must still trap.

Arrays use packed element widths: u8/i8 one byte, u16/i16 two, u32/i32/f32
four, u64/i64/f64 eight. Each region starts at an address aligned to its
element width; no padding exists between elements. Region base addresses
are backend layout, not IR identity. Struct cells retain their separate
existing layout. Loads/stores preserve little-endian scalar bits.

BITCAST admits any source/destination pair within exactly one of these
wire-tag sets: {2 u8, 6 i8}, {3 u16, 7 i16},
{4 u32, 8 i32, 13 f32}, {5 u64, 9 i64, 14 f64}. Same-type pairs are allowed.
It reinterprets bits without numeric conversion. Bool, void, references,
and pairs crossing sets are invalid IR.

RINIT is valid only for a function-local region owned by the current
function; its initializer register has exactly the element type and must
be definitely assigned. It fills every element with that scalar's bits,
without conversion. An initialization bit for each local region starts
false at function entry; RINIT sets it true only after the complete fill.
Control-flow joins intersect predecessor states, and both
RLOAD and RSTORE require the target local region's bit to be true.
Thus declaration-time initialization is explicit, and the validator need
not prove that an arbitrary loop of stores fills the entire region.
Repeating RINIT is well-defined and resets the whole region; it does not
allocate a second region or expose previous contents.

### 10.3 Floating-point types and canonical version selection

Wire tags 13 and 14 mean f32 and f64. Tags 10..12 keep their historical
reference/bytes/cells meanings. The producer must explicitly map its
internal float tags rather than serialize them as wire tags.
CONST keeps its eight-byte immediate: f32 occupies the low half with a zero
upper half; f64 occupies the complete immediate.

Existing BIN add/sub/mul/div, UN negation, and CMP eq/ne/lt/le/gt/ge accept
matching float operands. Float bitwise operations, shifts and remainder
are invalid. NaN comparisons follow IEEE semantics: only ne is true.
Arithmetic rounds to nearest, ties to even, retains subnormals, and does
not contract separate operators into fused operations. Finite arithmetic,
infinities and signed zeros follow IEEE-754 at the declared width.

CONST, MOV, RLOAD, RSTORE, RINIT and BITCAST preserve all float bits,
including signed zero and NaN payloads. Float negation toggles only the
sign bit. A NaN produced by arithmetic or a float-width conversion may
have any quiet-NaN payload/sign; exact-byte parity does not apply to those
new NaN bits. This latitude never permits changing a transported NaN's
bits. Tests compare such arithmetic results by NaN classification while
separately checking exact payload transport.

CAST has the following complete rules:

- Integer-to-integer conversions retain the existing checked conversion
  contract: an unrepresentable destination value traps 5.
- Float-to-integer first truncates toward zero, then saturates to the
  destination type's own signed or unsigned range, including narrow
  destinations. Negative-to-unsigned saturates to zero; infinities saturate
  to the appropriate endpoint; NaN converts to zero. Stored/reloaded and
  directly returned results have the same canonical integer value.
- Integer-to-float interprets the source according to its signedness and
  rounds to the destination width using nearest, ties to even.
- f64-to-f32 rounds nearest, ties to even, preserves signed zero and
  subnormals, and overflows to signed infinity. f32-to-f64 is exact for
  finite values and preserves infinity and signed zero. NaNs follow the
  quiet-NaN latitude above. Same-type float CAST is a bit-preserving MOV.
- Bool, void and reference types are excluded from CAST in this profile.

Ordinary trapping wasm float-to-integer conversion alone is not an
equivalent lowering. Native/backend conversion defects are implementation
bugs, not alternate definitions of these rules. Floating-point intrinsics
need separately specified subcodes and are not silently lowered as calls.

Select wire v7 if and only if the unit has a region, a float type, or an
opcode introduced by this profile. Otherwise retain the existing v1..5
selection rule. A gratuitous v7 encoding of an old-only unit is refused.
Old readers remain valid for their declared versions and must refuse v7.

### 10.4 Backend and qualification obligations

The wasm backend exports its memory as `memory` and every function by its
original unique ASCII name. That name is reserved against a function export
collision. Read-only initial bytes become active data segments; the source
read-only property is enforced for generated Omega instructions, not a
security barrier against a JavaScript holder of exported memory.
An immutable i32 global named `region.NAME` exports each module region's
unsigned wasm32 byte offset. JavaScript interprets the i32 global value as
unsigned before indexing memory. Region bases remain stable for the module
instance; local arrays have no exported address. Source names cannot
contain the dot separator. Float parameters and results use wasm f32/f64
for wire tags 13/14; the existing integer/bool ABI remains unchanged.
Consumers use a typed view of exported memory and call exported scalar
functions once per frame. There are no imports, WASI calls, implicit
rendering loop, or allocation through JavaScript.
All computed export names must be unique, including `memory`, function
names and `region.NAME`; a collision is INVALID rather than an invalid wasm
module emitted for a JavaScript engine to diagnose later.

Wasm traps are `WebAssembly.RuntimeError`; MVP does not preserve numeric
Omega trap codes. A producer reports ordinary source errors: 7 type
mismatch, 9 unsupported context, 14 table capacity, 20 write to text/view,
21 constant index out of bounds and 23 storage capacity. A v7 backend keeps
the existing named refusal exits and empty stdout on parse/type/capacity
refusal; unknown storage classes are INVALID, unsupported profile features
are UNSUPPORTED_OP or UNSUPPORTED_TAG, and unsupported wire versions are
VERSION. Refusal is not a successful empty module.

Required qualification includes native/wasm scalar and memory parity;
u8/i32/f32/f64 round trips; text data; local reinitialization on repeated
calls; region-boundary, negative and wide-index traps; refusal of read-only
stores and foreign local ownership; deterministic module bytes; an RGBX
gradient framebuffer checksum comparison; a real pinned renderer primitive
with a full-buffer pixel comparison; and a changed-store or missing-check
mutant rejected by those checks. Existing v1 parity and refusal suites must
remain unchanged and green. Host evidence does not qualify GPU execution.
The new wire also needs an independent read-only oracle that decodes its
region table, validates ownership/types/initialization, and executes memory
instructions. Native/wasm agreement alone is not sufficient if both share a
producer bug. Hand-encoded valid and malformed units challenge that oracle
and both backends; the original pinned C renderer remains an independent
pixel reference rather than a translation of the Omega implementation.
Required malformed-wire controls include missing or loop-carried local
initialization, foreign-local access, invalid/sentinel operands and export
collisions. Repeated-call checks include calls after a trapped invocation.
Conversion fixtures cover signed, unsigned and narrow destination
boundaries, float widths, infinities, NaNs and signed zeros.
