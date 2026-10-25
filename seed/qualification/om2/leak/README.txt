OM-2 qualification leakage lane (#748, L836): contract §8 evidence (a), revised by E11/E12.
All implementation is handwritten seeded OM-1 Omega in leakscan.om. External objdump/Python are
oracles only (../oracle/leak/PROVENANCE.txt). No compiler-lane or SHA-lane source was read.

Build/run (repository root)
  seed/out/omc < seed/qualification/om2/leak/leakscan.om > build/om2-qual/leakscan-e12
  chmod +x build/om2-qual/leakscan-e12
  build/om2-qual/leakscan-e12 [xN | pub:SLOT:OFF:LEN ...] < ARTIFACT
N/SLOT are entry parameter slots 0..5. xN marks DATA; otherwise entry parameters are public pointers,
lengths/opcodes. pub declares public contents for the full execution in bytes [OFF,OFF+LEN) relative
to the original entry pointer in that slot. Up to 32 ranges, decimal nonnegative OFF/LEN with sum
<= 2^55-1. An incorrect declaration is an unsupported caller assertion, not a proof by the scanner.
A store of secret data into a declared public range violates that assertion.
Exit: 0 PASS; 1 FAIL (data-index with no MANUAL); 2 NOT_RUN (any MANUAL, UNKNOWN or UNHANDLED);
90 invalid argv; 91 malformed artifact (empty, ELF, partial word, over 8 MiB); 92 worklist limit;
93 output I/O failure. NOT_RUN is never PASS.

Output
  @N means word offset N, byte offset 4N. Entry word 0 (or its direct B to the last function).
  BRANCH @N mnemonic ->@target len | data never-taken-trap-check | data MANUAL [trap=code]
  MEM @N mnemonic form base=reg index=reg const | public-index | data-index | unreached
  PUBLIC-DECL SLOT+OFF:LEN
  PUBLIC-MEM @N public-mem SLOT+OFF       EVERY load the declaration classifies public is listed here.
  UNKNOWN @N 0xword; UNHANDLED @N reason; SUMMARY class counts; VERDICT TRACE-EQUIV-STATIC ...
The legacy summary field trap-check counts only E12 PROVED checks; data-branch counts MANUAL branches
(trap targets included). MANUAL-ITEMS enumerates UNKNOWN/UNHANDLED; data MANUAL branches are listed
individually and counted in data-branch. All conditional branches are listed, including B.AL/NV.
All memory accesses with a base other than SP/FP, or any register index, are listed. SP+immediate
spill slots are modeled but not listed. Literal-pool loads are public, not an indexed address.

E12 safety correction (supersedes the original structural trap rule)
A branch to a trap stub is NOT sufficient proof that it is never taken. Required negative control:
  fn f a view ulong 4 to uint / ret uint get a 0 / end
This scans data MANUAL trap=5, verdict NOT_RUN. Native program-mode twins reachable-zero.om and
reachable-big.om execute that same loaded conversion with 0 and 2^32: gate checks exits 0 and 105.
Only a conversion CBNZ-to-trap whose check register is proven zero through an AND with a known
constant mask can be discharged. We track known-zero bits for GPRs: constants, MOVK, AND-immediate
(Arm DecodeBitMasks) or AND-register, copies and UBFM/LSR. AND-proof provenance must survive to the
check; canonicalization of a loaded narrow value alone is not an AND proof. Joins intersect zero
bits and proof bits. CBNZ with all bits known zero and AND provenance is data never-taken-trap-check.
The positive trap-check.om uses AND 0xffffffff followed by LSR #32; it remains PASS.
Other DATA branches, including structurally identified trap edges, are MANUAL and force NOT_RUN.
Memory data-index still fails absent a MANUAL item. Speculative execution, latency and caches are
outside the claim (§8 lines 239-241). This is finite static evidence, not a mechanized proof.

Decode subset (main acb3f15 omc emission, no candidate source dependency)
- B/BL; B.cond; CBZ/CBNZ; TBZ/TBNZ; RET/BR/BLR; SVC; NOP/DSB.
- ADD/SUB(S) immediate, shifted and extended; logical immediate and shifted-register; MOVZ/N/K;
  SBFM/BFM/UBFM; ADR/ADRP; ADC/SBC, CCMP/CCMN, CSEL/CSINC/CSINV/CSNEG (CSET alias).
- UDIV/SDIV, LSLV/LSRV/ASRV/RORV, RBIT/REV/CLZ/CLS, MADD/MSUB and widening/high multiply.
- EXTR/ROR-immediate, RORV and NEG were already covered by the instruction classes; the independent
  citations are Arm DDI0487M.d C6.2.160 p2201 (EXTR), C6.2.349 p2580 (RORV, DP2 opcode 11), and
  C6.2.295 p2475 (NEG = SUB from XZR, not SP). No S1 source was consulted.
- Scalar FP moves/conversions, one/two-source ops and comparisons, single/double precision.
- Loads/stores: unsigned immediate, register offset (UXTW/UXTX/SXTW/SXTX + scale), unscaled/pre/post,
  LDP/STP/LDPSW and LDR literal. Reserved/unallocated patterns are UNKNOWN, including invalid sf/N,
  logical masks, shifts, opcode/type fields, PRFM, and constrained-unpredictable writeback aliases.
Om1 emission references: omc.om ldop/stop 1321/1354; e3 1498; matconst 1568; canon 1595; cset 1653;
bo_* 1894-2083; scalar FP 2088-2355; save/loadarg/restore 2460-2524; elemaddr 2547; finish 5633-5670.
SIMD/SVE, atomics/exclusives, system-register instructions and other encodings are UNKNOWN.

Taint/CFG rules
- May-DATA bits for x0..x30, NZCV and v0..v31, OR at joins. Flag-setting ops REPLACE NZCV taint by
  sources; CSET/CSEL/ADC/CCMP read flags. Memory values DATA except literal loads, tracked spill slots
  and proven declared-public loads. Entry x7/runtime and x30/return address public; other nonparameter
  registers and flags start DATA. Constants/ADR/ADRP/zero-register public.
- Worklist to fixpoint; conditional branches have both successors (AL/NV only target). Direct calls
  join caller argument/register states into callee entry. Each RET joins into an exit summary, which
  requeues reached callers. Registers/flags after BL come from the callee summary; caller spill state
  is restored separately. This is context-insensitive: distinct call sites lose differing constants.
- SP depth and current 64 eight-byte spill slots are modeled. omc save/restore lowers SP, spills all
  live pool registers + x30, reloads args, BLs, reloads preserved values and releases the frame.
  Spill loads carry saved taint AND exact pointer/constant provenance, not unconditional DATA.
- SP writes other than ADD/SUB SP,SP,#imm, SP writeback, accesses outside the current frame, mismatched
  depths/functions, nonzero RET depth, DATA x30, indirect BR/BLR, SVC, out-of-artifact flow or writes
  to x7 are UNHANDLED and force NOT_RUN. Exact-provenance record exhaustion also does (provenance-cap).
- omc invariant A1: computed-base array writes never alias spill slots. Arrays precede calls on the
  stack, spill areas are below them, and runtime/compile-time index checks prevent cross-array writes
  (sa_arr 4922, elemaddr 2547). This invariant is assumed, not re-proved for arbitrary machine bytes.
- The trap shape is MOVZ X1,#1..14; B to MOV X0,X7; LDR X16,[X7,#16]; BR X16. Those paths terminate;
  the runtime trap is assumed nonreturning. Shape identifies the trap target, NOT its unreachability.

Public-memory provenance / P1 resolution
Each GPR carries unknown, an exact signed constant, or original-entry-slot base + exact constant.
MOV, ADD/SUB immediate and ADD/SUB register with constant shifted/extended operand propagate it;
MOVZ/N/K build constants. Narrowing a pointer loses its identity. Spill slots, function inputs and
exit summaries carry the same domain. Unequal provenance joins to unknown. No intervals, loop
induction or call specialization. Constants/offsets outside signed 56 bits are conservatively unknown.
Whole aligned eight-byte spills preserve identity; partial stores invalidate every overlapping slot.
All bytes of a load (both elements of a pair) must fit a declaration; no wrap or one-past-end admission.
P1 is resolved by EXPLICIT DECLARATION, not by silently declaring context memory public or treating
structural trap checks as safe. It works for constant layout-field accesses; the fixtures prove the
half-open boundary. Current OM-1 SHA getle(c,p,nb) shares call sites and loops over p+i, so its count
loads remain unknown in this exact domain even with pub:0:32:8. The 15 count-derived items are still
listed for manual classification, not passed. See leak-sha256-om1-notes.txt. E12 additionally leaves
nine unproved DATA trap checks MANUAL. Dynamic traces and the OM-2 SHA artifact are NOT_RUN here.

Cases/evidence (all paths relative to seed/qualification/om2)
leak/leak.cases runs now under 734f0a39; leak/candidate.cases runs common cases under a candidate and
compiles s1-rot.om. Binary input identities are in leak.cases; expected reports are hand-derived from
construction and objdump offsets, not copied as behavioral truth from a compiler under test.
evidence/phaseB/leak-pinned.log and leak-candidate-s1.log record gates; leak-sha256-om1.txt and
leak-sha256-om1-pub.txt hold full corrected scans; leak-s1-rot.txt records source/artifact identities,
scan and objdump cross-check. leak-reachability.txt records native negative-control gate outcomes.
leak-corpus-xcheck.txt: 31 baseline function-mode units cross-checked. leak-fuzz.txt: historical decoder
fuzz before E12; decoder classes unchanged, not evidence for the added provenance/known-bits analysis.

Mutant kills (exact exits plus stdout differs from the corresponding expected report)
cbtaint/trap-check: exit0, check mislabeled len; loadpub/data-branch: exit0 false PASS;
callexit/call-data: exit0 false PASS; slotoff/call-public: exit2 ret-x30-data;
flagsweak/flags: exit2, public compare wrongly DATA; nostub/trap-check: exit2 br-indirect;
pubrange/public-out: exit0 false PASS because one-past-end byte incorrectly declared public.
Native-reachability controls pin the earlier false-PASS defect. Candidate compiler leak mutants
must produce a new MANUAL branch, data-index, UNKNOWN or UNHANDLED item; they are never accepted
as PASS. Removing unroll is not a leak mutant (constant-bound loop branches remain public).
