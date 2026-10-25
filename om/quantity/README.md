# SC1-Q checked quantities

Implementation plan: (1) freeze API and fixtures, (2) implement checked operations and independent fixtures, (3) compile pinned seed and execute native tests plus named mutants, (4) record immutable evidence. Scope is only `om/quantity/`.

## Frozen API

`struct qty val ulong cap ulong end`; `struct qobj ident ulong ver ulong extent ulong end`; `struct qslic ident ulong ver ulong off ulong count ulong end`.

All functions return int: 0 success, 1 invalid operand (`val > cap`), 2 domain mismatch (different caps, negative signed ingress, widening via narrow), 3 overflow/underflow or extent violation, 4 object/version mismatch. Checks precede arithmetic and output stores. Every refusal leaves the complete output unchanged. Output must be a distinct struct from each input (seed alias rule).

- `qword(out edit qty, raw ulong, cap ulong)` and `qsigned(out edit qty, raw long, cap ulong)` validate original ingress, then write value/cap.
- `qadd/qsub/qmul(out edit qty, a view qty, b view qty)`: validate both operands, require equal caps, then checked arithmetic within that cap. Subtraction refuses b>a. Zero multiplication succeeds.
- `qnarrow(out edit qty, a view qty, cap ulong)`: validate a, refuse cap>a.cap as domain mismatch, refuse a.val>cap as overflow; no scalar cast before validation.
- `qnext(out edit qty, a view qty)`: validate a and refuse a.val==a.cap; increment without wrap.
- `qtyslice(out edit qslic, off view qty, count view qty, expect view qobj, current view qobj)`: compare all expected/current identity, version and extent fields, validate operands and require both caps equal current.extent, then check off<=extent and count<=extent-off. Writes binding plus offset/count, never forms an unchecked end address.

Raw words, struct construction, field writes and extraction are trusted boundaries, not sealing or provenance. In particular qword(add raw_a raw_b) cannot detect a prior wrap. Consumers must recompute from validated original operands at the effect boundary. A `ulong` identity is an owner-assigned nonreused object identifier, not an fd; the owner supplies the trusted current version/extent. Reusing both identity and version cannot be detected here. This library has no authentication, storage, parser, address dereference or service integration.

Equal caps are not physical units: two counts from different application domains
still have type `qty`. The consumer owns that domain distinction. To revalidate
a serialized slice, reconstruct distinct checked offset/count values from its
original fields and the trusted current extent, construct the expected object
from its recorded identity/version, then call `qtyslice` against the current
object immediately before the effect. Never treat a raw `qslic` as validated.

## Frozen expectations before implementation

Q1: `(18446744073709551608 + 16) mod 2^64 = 8`; checked add returns3 and leaves sentinel output unchanged. Q2: exhaustive widths1..8, all pairs in0..(2^w-1), compare add/sub/mul with independent exact arithmetic oracle within u64. Q3: u64 zero/max, malformed operands, domain mismatch. Q4: max*2 returns3 before multiplication; zero*max succeeds. Q5: signed -1 returns2 unchanged; signed max succeeds under u64 max. Q6: 256 narrowed to255 returns3 unchanged; 255 narrowed to255 succeeds; widening refuses2. Q7: zero and exact extent slices succeed, extent+1 refuses3. Q8: scalar passed to qty and qty passed to scalar are compiler error7; ordinary crypto-style unsigned wrap stays accepted. Q9: epoch max refuses3; stale version and reused identity with changed version refuse4; identity or extent mismatch refuses4. Every runtime refusal checks output preservation.

Mutation controls must compile and reach the named Q1_ADD_GUARD and Q4_MUL_GUARD assertions, with normal exit1. Build failures or signals do not count.

## Qualification boundary

This is a bounded library profile. Independent review is complete (see below). It is not language-wide constructor sealing, formal/native translation proof or a qualified consumer migration. The omnet transport/Revision consumer slice exists only as a proposal in aien-dev/omnet#6, with the remaining sites inventoried, not migrated. Those remain open under #756/#673. Native execution, finite exhaustive arithmetic checks, compiler-refusal fixtures and mutation sensitivity are separate evidence categories. No GPU or timing secrecy claim.

## Native replay

Run from repository root on Linux AArch64. The only production unit is `quantity.om`; `tests.om` is the executable fixture runner, `demo.om` is the demonstration, and `mutate.om` is a test-only exact source transformer. No generated binary is installed or committed.

```sh
nice -n 19 sh seed/omega-from-seed.sh
sha256sum seed/out/omc
# Required compiler hash: 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
mkdir -p /tmp/security756-replay
cat om/quantity/quantity.om om/quantity/tests.om | nice -n 19 seed/out/omc > /tmp/security756-replay/tests
chmod +x /tmp/security756-replay/tests
nice -n 19 /tmp/security756-replay/tests
cat om/quantity/quantity.om om/quantity/demo.om | nice -n 19 seed/out/omc > /tmp/security756-replay/demo
chmod +x /tmp/security756-replay/demo
nice -n 19 /tmp/security756-replay/demo
nice -n 19 seed/out/omc < om/quantity/fixtures/crypto-wrap.om > /tmp/security756-replay/crypto
chmod +x /tmp/security756-replay/crypto
nice -n 19 /tmp/security756-replay/crypto
cat om/quantity/quantity.om om/quantity/fixtures/mixed-scalar.om | nice -n 19 seed/out/omc > /tmp/security756-replay/refused-scalar
cat om/quantity/quantity.om om/quantity/fixtures/mixed-quantity.om | nice -n 19 seed/out/omc > /tmp/security756-replay/refused-quantity
cat om/quantity/quantity.om om/quantity/fixtures/mixed-object.om | nice -n 19 seed/out/omc > /tmp/security756-replay/refused-object
cat om/quantity/quantity.om om/quantity/fixtures/mixed-slice.om | nice -n 19 seed/out/omc > /tmp/security756-replay/refused-slice
nice -n 19 seed/out/omc < om/quantity/mutate.om > /tmp/security756-replay/mutate
chmod +x /tmp/security756-replay/mutate
nice -n 19 /tmp/security756-replay/mutate a < om/quantity/quantity.om > /tmp/security756-replay/add.om
nice -n 19 /tmp/security756-replay/mutate m < om/quantity/quantity.om > /tmp/security756-replay/mul.om
cat /tmp/security756-replay/add.om om/quantity/tests.om | nice -n 19 seed/out/omc > /tmp/security756-replay/mutant-add
cat /tmp/security756-replay/mul.om om/quantity/tests.om | nice -n 19 seed/out/omc > /tmp/security756-replay/mutant-mul
chmod +x /tmp/security756-replay/mutant-add /tmp/security756-replay/mutant-mul
nice -n 19 /tmp/security756-replay/mutant-add
nice -n 19 /tmp/security756-replay/mutant-mul
```

Expected exits: successful compiles, tests, demo and crypto control0; each mixed-type compile7; guard transformer0; each mutant compile0 and execution1, printing its named failed assertion. Never execute output from a refused compile. Check each command's exit individually; an arbitrary nonzero is not success.

The independent arithmetic oracle uses signed long operations on inputs0..255, whose exact sums/products fit (at most510/65025); it does not duplicate the library's overflow predicate. The runner exhausts87380 pairs across widths1..8 and262140 binary operations, plus all in-domain raw/signed constructors, next operations, all87380 narrowing pairs, and the zero-capacity unary fixture. This is finite differential checking, not a proof over u64. Extrema and object cases are separately named fixtures. Output sentinels check every tested refusal. The demo's side-effect counter is a local witness only, not a real service-effect test.

Observed evidence is additive under `evidence/`. It retains initial attempts, including the scalar literal syntax diagnostic16 before the fixture was corrected to pass a scalar variable and reach the intended nominal type diagnostic7. The compiled mutation controls both execute to their intended assertion; the transformer also refuses absent/duplicate guards (exit4) and an invalid mode (exit2).

Independent review found no implementation defect and requested additional u64
multiply fixtures. The runtime suite now refuses `2^32 * 2^32` and
`(2^32+1)^2`, accepts the exact `(2^32-1)^2 = 18446744065119617025`,
and exercises `0 * u64_max`. The second refusal defeats a multiply-first guard
that only compares the wrapped product against its cap and left operand.
Object-to-quantity and slice-to-quantity fixtures also refuse with compiler7.
These are boundary checks, not exhaustive u64 proof. The finite signed oracle
shares the compiler with the library; it is algorithmically independent, not
an independently compiled translation referee. Original evidence remains immutable;
review additions and final clean replay have separate receipts.

`oracle-export.om` emits actual helper results, with no reference arithmetic in
the program: width1..8, a0..cap, b0..cap, then add/sub/mul, three little-endian
u64 words per result `(status,value,cap)`. The external arbitrary-precision oracle
compares each exact integer result with `[0,cap]`; expected success is
`(0,result,cap)`, refusal `(3,17,19)`. All262140 records matched independently,
including every refusal sentinel. `evidence/review-exhaustion.json` binds the
6291360-byte output and exporter; the raw output is retained compressed beside it.
Replay by concatenating `quantity.om oracle-export.om` into the pinned compiler,
executing the resulting binary, and comparing its stdout SHA-256 and bytes to
the decompressed corpus. This supplements, rather than relabels, the original
same-compiler signed oracle.
