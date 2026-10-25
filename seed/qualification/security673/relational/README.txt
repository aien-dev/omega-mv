#758 relational comparison slice (SC1-L)

Scope and result
----------------
Owned implementation and test logic is seeded OM-1 .om. No compiler, scanner, signer, authority,
store, gate implementation or omnet source is replaced. FROZEN.txt preceded implementation;
SC1-L.txt defines entry labels, disclosure and architectural model. Source review is still required.

Measured native host results, not a general proof:
- Existing PR752 qualification gate: qualification.cases35/35, bindings.cases10/10,
  consumer.cases3/3 expected outcomes. Logs: evidence/run3/gate2.log,
  evidence/run3/bindings2.log, evidence/consumer/gate.log.
- compare32 native paired fixture completes24,577 equality checks: equal arrays plus all256 byte
  values at every32 positions and paired differing inputs with the same disclosed result0.
- Exact complete raw compare32 function is776 bytes,194 instructions. Scanner reports64 public
  indexed loads, zero branches, zero DATA addresses, zero UNKNOWN and zero UNHANDLED, PASS.
  embed.om finds the WHOLE raw function exactly once in the executed1872-byte native ELF.
  This mechanical containment witness is not a replacement translation checker.
- Secret branch, early exit, address, division-zero, explicit trap and direct-helper controls
  produce DATA-dependent scanner evidence and NOT_RUN (exit2), never a secrecy PASS. Gate PASS
  on these rows means the expected refusal was observed. No UNKNOWN item is counted as a kill.
- Branch/helper/address native twins both complete with output7. Deliberate error controls
  produce7/106 and division controls102/7. Those explicit trap controls are NOT mutation kills.
- Native-output mutants change the actual compiled raw code AND full ELF. Branch operand changes
  CMP x0,#0 to CMP x1,#0 (f100001f -> f100003f); address operand changes AND x4,x1,x3 to
  AND x4,x2,x3 (8a030024 -> 8a030044). patchword.om requires exactly one aligned old word.
  Native secret0/1 executions of BOTH mutants still complete with output7. Raw scanner changes
  from PASS to DATA branch/address NOT_RUN. No crash, timeout, build failure or unsupported
  instruction counts as detection. Whole branch-mutant raw code also matches its ELF exactly.
  The address probe has mode-specific trap tails, so raw and ELF are separately bound, not
  claimed byte-identical or translation-proved. The changed address-computing instruction is
  checked exact-once in each full artifact. Raw scanner input includes the whole trap tail.
- Instruction authority for those changes: Arm DDI0487M.d C6.2.96 p1973, CMP(immediate), and
  C6.2.15 p1832, AND(shifted register). Rn is bits9:5. Existing objdump is an external oracle,
  not first-party implementation; captured disassemblies are under evidence/run2/.
- The compiled finite nondeterminism model has4/4 ordinary inclusion checks succeeding while
  both same-choice secret pairs differ. Independent choice pairs agree. A source mutant that
  removes the leak reaches named assertion83; a wrong equality result reaches assertion13.
- Byte-binding negatives compile successfully then existing gate rejects changed source/native
  bytes and wrong checker output hash (binding-negative.cases, exit1, two FAIL outcomes).
  Wrong compiler hash produces gate INVALID3 with no case run. Wrong image reaches assertion61;
  missing patch word reaches assertion93. These are narrow test pins, not authenticated receipts.

Actual consumer audit
---------------------
OMNET-AUDIT.txt records the real disablement path and unresolved dependency closure. An unchanged
byte-exact dseqhash excerpt from helpers.om238-245 is retained as evidence/consumer/dseqhash.src,
with complete-file hash and byte range in provenance.json. The existing scanner identifies its
DATA branch@12; native inputs complete with1/0. This is a baseline audit finding, not consumer
qualification. ConsumerSecret/WORM owns any subsequent fixes and must bind its new source anew.
Argon2i/BLAKE2b, JSON/base64, authority-list iteration, retained copies, BLAKE2s snapshot hashing,
restart, cleanup and intended bootstrap disclosure remain outside this primitive's qualification.

Binding and limits
------------------
BINDINGS.json binds sources, compiler source/binary, exact raw and ELF bytes, scanner source/binary,
SC1 model/disclosure and policy labels. CONSUMER-BINDINGS.json adds the later audit fixture bindings.
These JSON records are UNSIGNED TEST RECORDS and not an alternate trust path. The scanner has no
API to authenticate target/model/disclosure metadata or decide that a return value is authorized.
Such mismatches remain BLOCKED_OWNER_623; wrong target/model/disclosure rejection is NOT_RUN.
The #623 owner must supply the relation and receipt adapter. L836/ORPHAN retains the scanner.

Paired outputs are actual native executions. Branch/address results are static scanner evidence,
not measured instruction traces. Dynamic native trace capture, mechanized relational proof,
physical constant time, cache/speculation/latency immunity, shared-resource secrecy, complete
consumer closure and deployment are NOT_RUN. The finite source-choice counterexample is not an
implemented nondeterministic source-language feature or a compiler-preservation theorem. No
external theorem is claimed. #759 must separately qualify scheduling/allocation/reclamation.

Evidence chronology
-------------------
run1 preserves initial compile failures (unsupported bool conversion and mixed signed length
comparison), successful scanner/gate builds and controls. run2 preserves the passing loop
primitive and first35-case gate. run3 uses explicit fixed indices to make the entire raw function
identical inside the ELF, not to evade a scanner refusal (the loop was already PASS). The first
bindings run has a malformed case token and is INVALID, preserved unchanged; bindings2 corrects
that token and completes10/10. Wrongbytes and wrongcompiler are expected refusal runs. Generated
.src files are immutable gate input snapshots, not a second scanner implementation or owner.

Reproduction
------------
Check out main00eb0e7106d2f8fc841e5e16eb624fa1bd029111 plus this slice as security673-relational,
and PR752155ff3178f35013e405ed40d09a7dd75ede2a403 as sibling security673-scanner. Never consume
live moving scanner or OM2 compiler source. Existing bootstrap is the only shell runner required:

  cd security673-relational
  nice -n19 sh seed/omega-from-seed.sh
  sha256sum seed/out/omc

Expected734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61.
From sibling security673-scanner, with P=../security673-relational/seed/qualification/security673/relational
and C=../security673-relational/seed/out/omc, use the existing gate (build it from the pinned source
with C, or use recorded P/evidence/run1/gate.bin after verifying its BINDINGS digest):

  nice -n19 "$P/evidence/run1/gate.bin" "$C" 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61 "$P/qualification.cases" "$P/evidence/replay-qualification"
  nice -n19 "$P/evidence/run1/gate.bin" "$C" 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61 "$P/bindings.cases" "$P/evidence/replay-bindings"
  nice -n19 "$P/evidence/run1/gate.bin" "$C" 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61 "$P/consumer.cases" "$P/evidence/replay-consumer"
  nice -n19 "$P/evidence/run1/gate.bin" "$C" 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61 "$P/binding-negative.cases" "$P/evidence/replay-negative"

Expected exits0,0,0,1. Scratch directories must not exist; do not overwrite prior evidence.
Gate rebuilds every case from source. Scanner-source compilation emits its pinned output hash in
bindings.cases. Binary mutation reproduction is four patchword cases in bindings.cases, not a
foreign-language patcher. Raw commands, stderr, stdout and source inputs are retained in evidence.
No GPU, network/deployment action, PR publication or ready-to-merge label was used.

Integration owner: run independent final-source review/replay and add root contributor attribution
as appropriate; this lane is restricted to this new directory. Attribution: AIEN coding assistants.
