# Omegese profile OM-2: contract for the compact checked SHA slice (#599, #625, #748)

Status: FROZEN 2026-10-25 by the #599 language-evolution coordinator (ledger L835) after two
independent reviews of the first draft (Codex Sol and Claude Opus, both HOLD; every finding is
resolved below). It governs #655, #745, #746, #665 and #747, and is additive to
`docs/one_layer/OMEGESE.md`, which stays normative for everything not stated here. A change after
freeze needs `omegese-om3.md`.

## 0. Profiles, opt-in and compatibility

- **OM-1** is the language of the pinned seed-born omc, sha256
  `734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61` (main `acb3f15c`).
- **OM-2 is opt-in per unit.** A unit is OM-2 iff its first line that is not a `note` is exactly `om2`.
  OM-1 refuses that line (fixture under 734f0a39). The words of sections 2-7 are reserved and
  recognised only in OM-2 units. An OM-1 unit keeps every name it has today, including the existing
  user functions `rotr` (`om/blake3/blake3.om:68`) and the names `fresh` and `at`.
- **C1 (meaning).** Every OM-1 unit keeps its verdict, error code and line, stdout, exit status, trap
  code and trap order.
- **C2 (bytes).** Every OM-1 unit in the frozen manifest `seed/qualification/om2/manifest-c2.txt` (the
  #748 lane enumerates it from `seed/rung4/tests`, the seed gate corpora, `om/**/*.om`, the om-units,
  the gate669 corpus, refusals included) compiles to byte-identical output, or to the identical error,
  under 734f0a39 and the OM-2 compiler. This is checked per manifest entry and is not claimed beyond
  it. omc.om itself is excluded; it must still pass the full birth (`seed/omega-from-seed.sh`: om4(omc.om)
  == omc(omc.om) == omc(omc(omc.om))).
- **C3 (om4 subset).** Because of the om4 == omc birth rule, the compiler's own source stays inside
  om4's language. omc therefore cannot read its own argv, so all modes are selected in-band (sections
  1 and 2). The compiler's identity is supplied by the harness, never self-reported.
- **C4 (opt-in optimization).** Code-changing transformations (section 4) apply only where an OM-2
  unit requests them. A missing proof never becomes permission to optimize: the compiler emits the
  checked code or refuses with a named obligation.

## 1. Diagnostics (#655)

- The human line stays exactly `omc: line N: error E` on stderr, and stdout carries only the artifact.
  Nothing goes to stdout on refusal. Compilation stops at the first error, as today.
- **In-band mode:** in an OM-2 unit, the line `om2 diag` (instead of `om2`) makes omc write one machine
  record after the human line:
  `omc-diag 1 code=E sev=S line=N col=C span=A-B expect=K found=H obligation=O profile=OM-2`
  - `A-B`: a half-open byte span into the exact input bytes.
  - `H`: at most 32 bytes, hex-encoded.
  - `K`: one of `value type name word number end newline`.
  - `O`: one of `- init ctor layout bounds growth regs`.
  - `S` is `incomplete` only when the failure is at end of input and some continuation of the input
    could be accepted. Two cases are `unknown`: a reference to a function or struct that is not yet
    declared, and an incomplete argument list of a call to a function whose signature has not been
    read. These depend on later text (#655 milestone B). Every other failure is `error`.
- Source identity: the harness pairs each diagnostic with sha256(input) and the compiler pin. A
  diagnostic from a different input digest is stale and must be discarded by consumers.
- Suggested repairs are never emitted.

## 2. Literals, expression boundaries and the formatter (#745)

- **Hex literal (OM-2 units):** `0x` followed by 1..16 digits `0-9a-f`. Its value is the unsigned
  integer, and it takes the type the place needs under exactly the decimal range rule (error 8
  outside). `0xffffffff` for `int` is error 8; there is no two's-complement reinterpretation.
  - Error 6: a `-` before a hex literal; `0x` with no digits; more than 16 digits; an uppercase digit
    or `0X`; `_`; any other character. Uppercase is refused so that each value has one spelling.
  - `0x2a` and `42` compile identically (fixture).
- **Expression boundaries (both profiles, unchanged):** every word has a fixed arity and expressions
  are prefix. The OM-1 codes for a statement with tokens left after its expression, and for a line that
  ends before an operand is complete, are pinned by fixtures. Grouping `(`…`)` (§9.10) is unchanged:
  a group adds no code and never changes which operands a word takes. OM-2 adds no grouping syntax.
- **Canonical formatter (in-band):** input whose first line is `om2 fmt` is not compiled. omc writes
  the canonical text of the rest on stdout. The rules:
  - single spaces between tokens; leading indentation preserved as given; no trailing spaces; LF line
    ends (CR refused, error 6); exactly one final LF;
  - every `note` line, and every `text` literal's bytes, preserved exactly;
  - nothing else is rewritten.
  - **Properties:** fmt is idempotent, and compile(fmt(x)) is byte-identical to compile(x) for every
    accepted x. Malformed input is refused with its compile error code and nothing on stdout.
  - The source digest of fmt(x) differs from that of x in general: identity is always of exact bytes,
    and the line mapping is identity because fmt never adds or removes lines (fixture).

## 3. Rotate (#665)

- In an OM-2 unit, `rotr x n` and `rotl x n` for unsigned `x` of type uchar, ushort, uint or ulong,
  with `n` of the same type. The result has `x`'s type, and its value is the rotation of the w-bit
  pattern (w = 8, 16, 32, 64). Signed `x` is error 7.
- `n` must be in 0..w-1; n = 0 is the identity. n >= w traps 7 at run time, the same as OM-1 shifts,
  including a constant n (OM-1 also compiles a constant out-of-range shift to the runtime trap,
  `omc.om:2394`).
- **Lowering:** uint/ulong with constant n in range gives one `ROR` (immediate). Other n gives the
  range check then `RORV`. uchar/ushort may use shifts; the result is exact.

## 4. Unrolling, inlining, folding and checks (#665)

- **`for i T K unroll`** (K a number 1..64, in an OM-2 unit) emits the body K times, with `i` bound to
  the constants 0..K-1.
  - A write to `i` in the body is error 7.
  - `break`/`continue` in an unrolled body is error 9.
  - Removing `unroll` must leave observable behaviour unchanged. A gate compiles both forms of every
    unrolled fixture and compares their runs.
- **`fn NAME ... to T inline`** (OM-2) marks a function for inlining. Rules:
  - The function must not be recursive and must contain no `new` (error 9).
  - Arguments are evaluated left to right before the body, exactly as for a call.
  - Traps, their order and their codes are unchanged, and removing `inline` must leave behaviour
    unchanged (same gate).
  - Calls to functions without `inline` stay calls.
- **Parameter slots (function mode):** an unsized view or edit takes two of the six parameter slots,
  pointer first and then length; a sized array, layout or `out` takes one slot (its pointer); a scalar
  takes one. More than six slots is error 9. The bridge passes the slots in this order.
- **Folding:** only inside unrolled or inlined bodies, an operation whose operands are all constants is
  computed at compile time with exactly OM-1 arithmetic. An operation that would trap is not folded and
  stays the runtime trap, so a guarded `if ne i 0` / `div 100 i` compiles and behaves as before.
- **Check elimination:** an index into a SIZED array that is a constant after unrolling or folding is
  checked at compile time (error 21 outside) and gets no runtime check. Every other index keeps its
  runtime check. That includes every index into an unsized view (section 6) and every conversion range
  check. No other check is eliminated in OM-2.
- **Resource bounds:** a function's emitted words after unrolling and inlining are at most 65536, an
  unrolled body at most 64 copies, and inlining at most 4 levels deep. Anything beyond that is
  error 14 with `obligation=growth`. The 16-register limit stays error 12 (`obligation=regs`): there is
  no spilling. Each bound limits compiler work and memory linearly in the output. The per-function
  stack of an inlined body counts toward the caller's frame under the OM-1 4 MiB local-array rule.
- **Source mapping:** every emitted instruction keeps its source line. With `om2 diag`, a runtime trap
  site table is written after the artifact on stderr as `omc-site 1 line=N iter=k` records (k for
  unrolled copies, `-` otherwise). This does not change stdout.

## 5. Initialization and representation invariants (#746)

- **Fresh scratch:** `NAME edit T N fresh` (OM-2; N ≤ 64) tells the compiler the incoming contents are
  unspecified.
  - The compiler keeps one may-be-unwritten bit per element, and intersects the written sets at every
    control-flow join. A rolled loop contributes nothing, because no induction is attempted.
  - A `get NAME i` with a constant i (after unrolling and inlining) whose bit is not definitely written
    is error 27 (`obligation=init`). A `get` with a non-constant index is error 27 unless all N bits are
    definitely written.
  - Passing NAME to a non-inline call, to `read`/`write`, or to `part` is error 27.
  - Storage being initialized is distinct from the logical range the caller uses: `fresh` promises
    nothing about what is left behind.
  - Analysis storage is one 64-bit set per open block, bounded by the 512-block limit.
- **Abstract types:** `abstract NAME T` (OM-2; NAME ≤ 8 characters; T uint or ulong) declares a type
  representation-equal to T but distinct from it.
  - **Construction:** `seal NAME v` is allowed only in the function named `NAME` followed by `mk` (for
    example `dup32mk`), which must return NAME. A `seal` anywhere else is error 28
    (`obligation=ctor`).
  - **Use:** `open v` gives the T value. A T value or a bare number where NAME is required is error 7.
  - **No external values:** NAME may not be a parameter or return type of the unit's entry function, and
    there are no arrays of NAME. External bytes therefore never become NAME without passing through the
    constructor.
  - **Preserving operations**, from a fixed table: `rotr NAME n` and `rotl NAME n` (64-bit rotation of a
    dup value is the dup of the 32-bit rotation by n mod 32), and `and`/`or`/`xor` of two NAME values.
    Each returns NAME, and is allowed only when the `abstract` line lists it (`abstract dup32 ulong ops
    rotr xor`). Every other operation needs `open` first.
  - **Invariant:** `holds NAME dup32` attaches the one built-in predicate of OM-2, `dup32`:
    high32(v) == low32(v). The constructor body must be exactly
    `ret seal NAME or shl ulong x 32 ulong x` with `x` a uint parameter. uint values are held
    zero-extended in registers (`omc.om` conversion rule), which is the premise of the #748 arithmetic.
    With that exact body the check is erased, rule `dup32`. Any other constructor body keeps a runtime
    check (trap 10 on false). Any other predicate is error 28. `om2 diag` reports every kept and every
    erased check as `omc-check 1 kept|erased line=N rule=dup32|-`.
  - Trap 10 is also available to user code, so the trap site, not the code alone, identifies a
    violation.
- **External layout:** `layout NAME SIZE` followed by one line per field
  `field F T [COUNT] at OFF little|big`, then `end`, before the first `fn` (OM-2; NAME and F ≤ 8
  characters; SIZE 1..4096; COUNT 1..4096).
  - A parameter `c edit NAME` or `c view NAME` takes a `uchar` array of exactly SIZE bytes (ABI:
    `(uint8_t *ptr)` with SIZE fixed, alignment 1).
  - `get c F [i]` and `put c F [i] v` access the field at its declared width and endianness, with the
    index checked like an array's.
  - Overlapping fields, a field past SIZE, and a COUNT or OFF out of range are error 29
    (`obligation=layout`).
  - Structs (§9.7: 8-byte cells) are unchanged and never assumed C-compatible.
  - SHA's context: `layout shactx 104`, `field state uint 8 at 0 little`, `field count ulong 1 at 32
    little`, `field buf uchar 64 at 40 little`, `end`. This must equal `src/sha256.h` on LP64 AArch64
    Linux; an `offsetof` oracle checks it outside the delivered closure.

## 6. Variable-length views and native ABI v2 (#747)

- **Unsized views (OM-2):** `NAME view T *` and `NAME edit T *` are parameters with run-time length
  `len NAME` (ulong). Every element index is checked at run time (trap 3); none is eliminated.
  `read`/`write`/`pread`/`pwrite` keep their slice checks. A local `new` stays sized.
- **Subview:** `part A o k` is the view of the k elements of A starting at o.
  - It is formed only after the checks `o <= len A` and `k <= len A - o`, in that order; otherwise it
    traps 3. The wrapped `o + k <= len A` is never used (#748 golden: len = 2^64-1, o = 2^64-2, k = 4).
  - `o` is evaluated before `k`. A `part` is allowed only as a call argument.
  - A part of a view is a view, and a part of an edit is an edit or, with `view` in front, a view.
  - For the distinct-names rule (error 19), a part counts as its base array, so two parts of one array
    in one call are error 19 whatever their ranges. No range-disjointness reasoning is attempted.
  - A part lives only for the call it is passed to.
- **ABI v2** (omega-bridge `docs/NATIVE-ABI.md`, sole writer L837). These rules add to every v1 rule
  and check, including the v1 address-wrap and result-overlap checks:
  - **Symbol versioning:** a unit with any unsized view or layout parameter is bound as
    `omega2_<name>`. No `omega_<name>` is emitted for it, so a v1 caller fails at link time, which is the
    explicit refusal. Every language binding uses the versioned symbol.
  - An unsized view argument is `(const T *ptr, uint64_t len)`, or `(T *ptr, uint64_t len)` for edit,
    with `len` counted in elements.
  - **Before any Omega code runs**, the bridge returns status 32 (refuse) if:
    - `len > 0` and `ptr == NULL`;
    - `ptr` is not aligned to sizeof(T);
    - `len * sizeof(T)` overflows 64 bits;
    - `ptr + len*sizeof(T)` wraps;
    - an `edit` range overlaps any other array argument's range or the result buffer. Ranges are
      half-open, and an empty range overlaps nothing.
  - `len == 0` with any pointer (NULL included) is an empty view.
  - Status 33 keeps its v1 meaning (reentry).
  - **Outputs:** a parameter declared `NAME out T N` (OM-2, sized) is written to a binding-owned buffer
    and copied to the caller's pointer only after status 0, with memmove semantics. It may therefore
    overlap inputs, as `sha256_hash(buf, n, buf)` does today, and nothing is written to it on a trap.
  - **Unsupported (caller precondition, not detected):** the bridge does not check that the memory
    exists for `len` elements, that it is writable, or that it stays alive during the call. Forged
    lengths and read-only memory passed as edit are UNSUPPORTED and outside the guarantee.
  - **Stack:** function-mode stack exhaustion is not converted to a status. It faults as in v1
    (NATIVE-ABI.md), and this is UNSUPPORTED.
- **Multi-entry units:** a unit may export several entries with `export fn NAME ...` (OM-2); each gets
  its own versioned symbol. A non-exported function stays private. Without any `export`, the v1 rule
  (the last `fn` is the entry) holds.
- **Module boundary:** separately compiled units are linked only through their exported symbols, and
  each binding embeds its unit's source sha256, the omc pin and the profile (`OM-1` or `OM-2`).
  `om/units.sh --check` refuses a stale unit, compiler or profile. A test links two units into one
  binary and calls them interleaved.

## 7. SHA consumer semantics (#747/#748)

- The API `sha256_init/update/final/hash` of `src/sha256.h` is preserved, including updates of length 0
  or at least 2^32, any split points, and `sha256_hash` with digest overlapping data. The four
  functions become `export` entries of the SHA unit. If the C names cannot be emitted directly by the
  bridge, a generated declaration header (bridge output, not handwritten C) provides them. No
  handwritten C logic remains.
- **Post-final:** after `final`, the context's contents are unspecified, and memory safety is the only
  guarantee. An `update` or `final` without `init` gives an unspecified digest. A receipt shows that
  init → update → final after a final equals the same sequence on a fresh context. No whole-context
  byte equality with the replaced implementation is claimed.
- `hash` of n bytes equals init → update(n) → final (receipt).

## 8. Effects, traps and leakage

- **Effects:** the SHA unit makes no system calls, and writes only to its `edit`/`out` arguments.
- **Traps:** codes 1..14 as in OM-1, with 10 also used by kept invariant checks. New compile errors:
  27 init, 28 ctor, 29 layout. Error 24 keeps its OM-1 meaning (branch displacement).
- **Leakage model:** the observer sees the executed instruction-address sequence and the data-address
  sequence on the host CPU, not values.
  - **Required property, trace equivalence:** for two messages with equal lengths, op codes and call
    sequence, the SHA unit's executed instruction-address and data-address traces are identical.
  - **Evidence:** (a) a static scan of the emitted code listing every conditional branch and every
    indexed address, each classified as length-dependent or data-dependent; a data-dependent branch
    must be a never-taken check (for example the uint conversion range check after `and x 0xffffffff`),
    and is listed; (b) a dynamic comparison of traces for seeded message pairs, where tooling permits;
    otherwise NOT_RUN.
  - Never-taken data-dependent branches are a speculative-execution channel. Speculative constant-time
    is UNSUPPORTED, as are claims about instruction latency or caches. No constant-time claim is made
    beyond trace equivalence on the tested host.

## 9. Evidence classes

Every acceptance result is labelled SOURCE (text or static check), HOST (GB10 host CPU, AArch64
Linux), EMULATED (qemu or a simulator), or PHYSICAL-GPU (none in this slice). RULE means a stated
static rule plus its fixtures; it is not a mechanized proof. Differential tests are finite. NOT_RUN is
never PASS. Authors do not certify their own lane: the #748 lane (L836) qualifies independently.
