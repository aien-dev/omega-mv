OM-2 qualification lane s5s7 (#748, ledger L836, QualPhaseB.FixInvariants)
==========================================================================

Scope: contract spec/omegese-om2.md (frozen, sha256 245bb74e...0881) sections 5 (fresh, abstract
types and dup32, external layout), 6 (unsized views, part, out, multi-entry, ABI v2) and 7 (SHA
consumer semantics), with errata E1-E9 (omega om2/contract). Written from the contract,
the errata, OMEGESE.md section 9, main acb3f15 sources and omega-bridge origin/main docs/NATIVE-ABI.md
only. No candidate-compiler, generator or SHA-lane branch was read.

Files
-----
  f5-*.om   section 5 fresh scratch            a5-*.om   section 5 abstract / dup32
  l5-*.om   section 5 external layout          v6-*.om   section 6 views, part, slots, out
  m6-*.om   section 6 multi-entry              abi/*.om  units for the ABI v2 driver (abi1.om is OM-1)
  om1/*.om  OM-1 twins and C1 controls         pr.om     putd/puth output helper (OM-1 words)
  diag/*.has  expected omc-check/omc-site lines for `om2 diag` (gate emit STDERR has:)
  candidate.cases    full list for a candidate OM-2 compiler (each row cites its contract line)
  pinned.cases       what runs now under 734f0a39 (96 cases)
  mutant-side.cases  predicted behaviour of compiler mutants (a)-(d); (e)/(f) are ABI level
  ../../oracle/s5s7/ python3 + gcc oracles, expected outputs (exp/), C drivers, PROVENANCE.txt

Every fixture starts with note lines naming its contract lines and property, then `om2` or
`om2 diag`. Program-mode fixtures are run as SRC = fixture + pr.om; function-mode fixtures (no
fn main) run alone, because pr.om's `text` is program-mode only and would become the entry.

Results recorded
----------------
  pinned.cases under the pinned omc:   evidence/phaseB/s5s7-pinned.log
  offsetof oracle (gcc, sha256.h):     evidence/phaseB/s5s7-offsetof.log  (layout matches; image == python)
  SHA driver self-check vs current C:  evidence/phaseB/s5s7-sha-selfcheck.log (validates the driver and
                                       the python digests only; NOT OM-2 evidence)

Assumptions where the contract is silent
----------------------------------------
A1  Refusal line: RESOLVED by erratum E8. 27/28/29 rows are exact `emit SRC - err:LINE:CODE CODE`
    (27: get/call line; 28: seal line, holds line for a predicate; 29: field line, layout line for
    SIZE, and for a layout after the first fn, E9). Other codes stay `refuse` (code only).
    The E6 reserved-name rows name the declaring line (as OM-1 does for `fn break`), which also
    separates them from the pinned compiler's om2-line refusal.
A2  `abstract` and `holds` are top-level lines before the first fn (placement unstated; follows the
    layout rule, line 154, and struct, OMEGESE 9.7). `holds` follows its `abstract`.
A3  f5-inline: `fn ... to void inline` is valid (line 93 shows `to T inline`), and since only
    NON-inline calls are 27 (line 127) and constants are taken "after unrolling and inlining"
    (line 124), writes made by an inlined callee count as definite writes.
A4  RESOLVED by E8: an operation the `abstract` line does not list (and, rotl, add) is error 7.
A5  RESOLVED by E8/E9: rotr/rotl of an abstract over ulong rotate the 64-bit pattern (line 140),
    n has the representation type T (E9), and n = 64 traps 7 (E8).
A6  (confirmed by E8) `omc-check 1 kept|erased line=N` names the constructor's `ret seal` line. A kept check is a runtime
    trap-10 site, so `om2 diag` also writes `omc-site 1 line=N iter=-` for it (lines 114-116); the
    order of omc-check and omc-site records is unstated, hence has: (order-free). The erased fixture
    is function mode with no other trap site (constant in-range shifts carry no check in OM-1).
A7  Layout field types used: uchar, ushort, uint, ulong (unsigned only); `get` returns the field type,
    `put` takes it; `get c F` (scalar) and `get c F i` (COUNT given) per erratum E5.
A8  (confirmed by E8) a constant field index outside COUNT is error 21; put through `c view NAME`
    is error 20; a uchar array of another length (103 for SIZE 104) is error 7.
A9  Inside a callee, `len` of a part is k, and a sized array passed to an unsized parameter has
    `len` = N (E2).
A10 ABI v2 C shape (driver.c): `uint32_t omega2_<export>(uint64_t *result, ...)` as v1; unsized view
    = (ptr, uint64 len); layout and out = one pointer (line 157, E1); pointers declared void *.
A11 `omega2_ident_<unit>()` returns a NUL-terminated `const char *` that contains the source sha256
    (64 lowercase hex), the omc pin (64 hex) and the word OM-2. Its exact format is unstated (E1).
A12 A NULL layout or result pointer is refused 32 (v1 null-pointer and null-result rules kept, line 181).
A13 The bridge-generated SHA header's file name is passed to sha_driver.c as -DGENHDR; it declares
    the type `sha256_ctx` and the four sha256_* names (line 214, E3 mapping to omega2_sha256_*).
A14 Multi-entry: how several exports appear in the function-mode artifact is unstated; only
    acceptance is checked in candidate.cases, symbols at the ABI level (driver, nm).
A15 A `fresh` parameter may be declared on an internal program-mode function.

Ambiguities, conflicts and proposed fixes
-----------------------------------------
Q1  Line 14 "OM-1 refuses that line": observed false for most OM-2 units. The pinned omc's names
    pass (OMEGESE 9.2) reads every `fn` signature before any other line, so a unit whose signature
    uses OM-2 syntax (fresh, `view T *`, `edit shactx`, `p dup32`, `to dup32`) is refused
    `error 16` at the FIRST such fn line, and `fn ... to void inline` gives `error 5` at that line
    (a token after a complete signature; code not documented in OMEGESE, recorded in pinned.cases
    as OBSERVED-REFERENCE). Only units with OM-1-valid signatures are refused at the om2 line
    (of the 80 OM-2 fixtures here, 61 refuse at a signature line: 60 with error 16, f5-inline with
    error 5; 19 at the om2 line; evidence: s5s7-pinned.log). Proposed fix:
    "OM-1 refuses the unit with error 16 at the om2 line or at an earlier-read signature line."
Q2  RESOLVED by erratum E8: fresh N > 64 -> 14; abstract type at an entry or as an array element -> 7;
    layout SIZE 0 / > 4096 and layout after the first fn -> 29; part outside a call argument -> 9.
    The former NOT_RUN rows are now cases (f5-n65, a5-entry-param, a5-entry-ret, a5-array,
    l5-size0, l5-size4097, l5-after-fn, v6-part-local).
Q3  RESOLVED by E8: a missing index on an array field -> 4 (l5-count1-noidx); an extra index on a
    scalar field -> 5 (new l5-scalar-idx).
Q4  Line 179 "a part lives only for the call" has no observable fixture: no word can store a view,
    and part outside a call is error 9 (v6-part-local). The property is structural (SOURCE/RULE only).
Q5  Line 189 (ptr not aligned -> 32) conflicts with line 194 (len 0 with ANY pointer is an empty view)
    for a misaligned pointer with len 0 (abi-misaligned-len0, expectation not asserted). Proposal:
    len == 0 is checked first and wins.
Q6  Line 191 "ptr + len*sizeof(T) wraps": an end address exactly 2^64 (last byte 2^64-1) is ambiguous;
    the driver avoids it (abi-wrap-high uses an end of 2^64+8). Proposal: refuse when
    ptr + bytes > 2^64 - 1 computed in 65 bits, or state the rule explicitly.
Q7  Line 173 with 175: whether `o <= len` is checked before k is evaluated is unstated (o out of range
    and a trapping k expression). Fixtures avoid the combination; v6-part-order uses a trapping o.
Q8  `out` parameters are specified only at the ABI boundary (196-198); their meaning on an internal
    call inside a unit (program mode) and whether an out may be read are unstated. Only exports with
    out are compiled (v6-out) and run through the driver (abi-out-*).
Q9  `holds NAME dup32` requires T = ulong in practice (high32/low32); `abstract NAME uint` with
    `holds ... dup32` is unspecified. Not tested.
Q10 Line 164's `field count ulong 1 at 32 little` is superseded by erratum E5 (scalar); fixtures use E5.

ABI v2 driver: build and run (all ABI and SHA rows are NOT_RUN in candidate.cases until AbiLane's
v2 generator and ShaLane's unit exist). Steps, from the worktree root, with CAND = candidate omc:
  1. Compile units: CAND < sec/s5s7/abi/abiv.om > abiv.bin; same for abiw.om; abi1.om (OM-1) with
     both the pinned omc and CAND (C2: identical bytes expected).
  2. Generate one binding for units abiv, abiw, abi1 with the v2 generator (input framing as v1
     tests/native/run.sh: source, 0 byte, 8-digit length, code); the harness supplies the omc pin.
  3. cc -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC -shared -o libabi.so <binding>.c
     cc -std=gnu11 -O2 -Wall -Wextra -Werror oracle/s5s7/abi/driver.c -L. -labi -Wl,-rpath,. -o driver
  4. ./driver $(/usr/bin/sha256sum < abiv.om | cut -c1-64) $(/usr/bin/sha256sum < abiw.om | cut -c1-64) \
       734f0a39...6b61-or-the-candidate-pin    -> one PASS/FAIL line per abi-* id, exit 0 iff all PASS
  5. abi-v1-absent: `nm -D --defined-only libabi.so` lists omega2_vsum and NOT omega_vsum, and
     `cc oracle/s5s7/abi/v1link.c -L. -labi` FAILS with an undefined reference to omega_vsum.
     abi-v1-kept: omega_madd1 present, omega2_ident_abi1 absent. m6-export2 bound alone: omega2_inc and
     omega2_dbl present, no symbol for `helper`.
  6. Stale identity (om/units.sh --check, line 209, E1), in a scratch copy of the tree with the units
     registered: (i) append one byte to a registered OM-2 unit after generation -> --check must exit 1;
     (ii) run --check with an omc whose sha256 differs from the pin -> must not print PASS (exit != 0);
     (iii) record profile OM-1 for an OM-2 unit -> must refuse.
  7. SHA (section 7): cc -DGENHDR='"<generated header>"' -I<dir> -Ioracle/s5s7/sha oracle/s5s7/sha/sha_driver.c
     <SHA binding/library> -o sha_driver; ./sha_driver big -> 16 sha-* lines, exit 0 iff all PASS.
     The 2^32+3-byte cases read 4 GiB from the shared zero page (mmap PROT_READ, no resident memory).

Mutant kill specs
-----------------
A kill pairs a PASS of the correct-side row (candidate.cases, candidate compiler) with a PASS of the
mutant-side row (mutant-side.cases, run with the mutant compiler as OMC) plus the mutant's control
rows (ctl-*). A signal, timeout or any other exit is never a kill.
(a) Weakened part check `o + k <= len` (wrapped). Correct: v6-part-wrap exit 103, empty stdout.
    Mutant: (2^64-2)+4 mod 2^64 = 2 <= 10, the part is formed, the callee returns its len without
    touching an element: stdout "4\n", exit 0 (mut-a-part-wrap, exp/v6-part-wrap.mutant).
    Related: second check dropped -> v6-part-tail prints "4\n" (mut-a2); first check dropped ->
    v6-part-o prints "0\n" (mut-a3); k evaluated before o -> v6-part-order exits 105, not 102 (mut-a4).
    The literal in-unit golden (len 2^64-1) is NOT_RUN: unreachable without a 2^64-1-element view; at
    the bridge the golden extent is abi-golden-wrap (status 32).
(b) Fresh analysis with UNION at joins. Correct: f5-join-diff refused `omc: line 12: error 27` and
    f5-branch `omc: line 10: error 27` (E8 lines).
    Mutant: f5-join-diff compiles and prints "99\n" exit 0 (else arm, element 0 keeps main's 99);
    f5-branch prints "7\n" exit 0 (mut-b-join-diff, mut-b-branch).
(c) Constructor bypass allowed. Correct: a5-seal-main and a5-seal-other refused
    `omc: line 11: error 28` (the seal line, E8).
    Mutant: both compile, exit 0 (mut-c-seal-main, mut-c-seal-other: emit any any 0).
(d) dup32 check erased for a non-exact body. Correct: a5-kept exits 110 with empty stdout, and
    a5-kept-diag reports `omc-check 1 kept line=8 rule=-`. Mutant: a5-kept prints
    "0000000500000006\n" and exits 0 (mut-d-kept); diag says `erased line=8 rule=dup32` (mut-d-kept-diag).
(e) Stale generation/profile identity accepted. Correct: driver abi-ident-abiv/abiw PASS (the
    ident text contains sha256 of the exact unit bytes, the pin and OM-2), and units.sh --check
    refuses each of the three stale cases of step 6. Mutant generator embedding a stale digest (for
    example of the previous source), the wrong pin or profile OM-1: driver prints
    "FAIL abi-ident-abiv" and exits 1. Mutant --check that skips the identity comparison: exits 0 /
    prints PASS on a stale unit, compiler or profile where the correct one exits non-zero.
(f) Bad ABI length accepted. Correct: status 32 with the result untouched. Mutant bridge without the
    check: abi-null-len1 returns status 0, result 1 (bcnt never reads an element); abi-golden-wrap
    returns 0, result 2^64-1; abi-size-overflow returns 0, result 2^61 and abi-misaligned 0, result 1
    (vcnt never reads an element); abi-edit-overlap returns 0, result 8 and modifies buf. Each
    prints "FAIL abi-..." and the driver exits 1: a clean, signal-free kill.

NOT_RUN in candidate.cases (never PASS)
--------------------------------------
  code unstated: none since erratum E8 (the 9 former rows are cases now)
  ABI v2 generator missing (AbiLane L837): 31 abi-* rows
  OM-2 SHA unit/header missing (ShaLane L838): 16 sha-* rows
  in-unit part golden with len 2^64-1: unreachable without a 2^64-1-element view (coordinator)
