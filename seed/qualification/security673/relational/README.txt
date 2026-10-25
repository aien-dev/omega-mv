#758 relational comparison slice, SC1 revision2

Current contract: ced61decef1ff4efbf5bc81598db648aa810be434e373e9bc1563dbcb4523496.
Current records: CURRENT-BINDINGS.json, FROZEN-R2.txt, SC1-L.txt, SHA256SUMS.
BINDINGS.json, CONSUMER-BINDINGS.json and FROZEN.txt are HISTORICAL DRAFT records for commit
4883943fd993401a3701d95f9c5fa031a6c887ea, not current-source qualifications. Their old L1-L8 IDs
and draft8bc52 contract are not SC1 revision2 IDs. Historical run1/run2/run3 evidence and the
parent's initial-clean-replay.json are preserved. No old PASS is relabeled as a new qualification.

Obligation matrix
-----------------
L1 VERIFIED finite/native fixture: source output{0,1} independent of secret, faulty result=secret.
   Ordinary inclusion4/4; both same-choice secret pairs differ; correct pairs agree. Assertion83
   catches a compiled leak-removing control. This is not a compiler preservation theorem.
L2 VERIFIED bounded component tests/static scan: compare32 has fixed public addresses/extents,
   secret bytes and final equality-only disclosure. Whole776-byte raw function occurs once in
   the current executed native paired harness. No real-daemon qualification follows.
L3 VERIFIED emitted controls: actual raw and ELF branch/address mutations preserve output7 for
   secret0/1 but trigger DATA scanner refusal. No crash/build failure/UNKNOWN counts as a kill.
L4 BLOCKED stronger division policy: current scanner PASSES dividend.om (secret x0 divided by
   constant7, then xor(q,q) returns0). SC1 revision2 forbids secret-dependent UDIV/SDIV, so that
   artifact is UNQUALIFIED despite scanner PASS. The existing scanner owner must add reviewed
   enforcement or approve explicit division-free admission. Secret divisor zero-check, helper
   and explicit trap controls do produce DATA refusals, but do NOT establish division policy.
L5 BLOCKED general target/model/disclosure/checker trust binding under #623/#624. Byte controls
   are VERIFIED tests: re-derived source/native artifacts, actual mutated checker-output hash,
   wrong-source bytes, wrong-image containment and compiler pin refusal. No alternate signer.
L6 BLOCKED real function-to-daemon correspondence: relevant emitted words AND call/return and
   observation contracts must be bound. Test-harness containment is not daemon correspondence.
L7 BLOCKED actual consumer/whole closure qualification. Captured original dseqhash baseline has
   DATA early-exit branch@12; native outputs1/0 and closure inventory are evidence of the baseline,
   not approval of either an old base or a later consumer proposal. Consumer owner owns fixes.

Measured repaired suite
-----------------------
The pinned existing PR752 gate completed qualification.cases56/56, bindings.cases16/16 and
consumer.cases4/4 expected outcomes. binding-negative.cases correctly returns1 with2 FAIL rows.
Logs and exact compiled inputs/outputs: evidence/run4/{qualification,bindings,consumer,wrongbindings}.
These gate PASS counts include expected scanner refusals and the explicit L4 unsupported-policy
witness. They are not blanket SC1-L or service PASS counts. Independent final review/replay pending.

paired.om now executes4,186,878 comparison assertions: each of32 positions, all256 base values,
all255 nonzero mismatch masks, and complemented paired inputs with the same mismatch. It also
checks identical flips at two positions and all32 positions for every mask, plus complements.
The exact same checker compiles with the XOR-instead-of-OR accumulator mutant and normally reaches
assertion16 at the first two-position cancellation. The constant-validity mutant reaches13.
This repairs the original narrow test, which only covered differences1/129 and let XOR survive.

Whole raw compare32 remains776bytes/194 instructions. Existing scanner reports64 public-index
loads, zero branches, zero UNKNOWN/UNHANDLED: PASS for its architectural model. Current native
paired harness is separately hashed in CURRENT-BINDINGS.json. The wrong-image containment test
now searches the2184-byte embed executable, larger than776, and fails hits!=1 with assertion61;
it no longer tests only the an>bn early refusal. embed.om is mechanical byte containment only.

Before scans, qualification.cases re-compiles EVERY recorded raw control and compares emitted
bytes directly against the exact scanner-input file. It also re-derives paired/native mutation
bases and reproduces raw/ELF mutation outputs with patchword.om. consumer.cases re-derives the
captured dseqhash excerpt's raw output. bindings.cases independently re-derives its native/raw
bases and wrong image. File-byte comparisons are stronger than accepting self-reported hashes.

The wrong-checker control now compiles the ACTUAL pinned scanner with exact-once source mutation
rvfix pass bound33->34. Its executable hash changes to73b682c9e26f30520b9cfd3030a706e77912303c7bedc582889f9a902b855cee,
which the existing gate rejects against the originalaa60061e... pin. It is not branch.om compared
to a scanner hash. No scanner working-tree file is edited or deployed; mutated .src/.elf files are
immutable test artifacts, not a second scanner implementation.

Emitted mutation details
------------------------
CMP x0,#0 -> CMP x1,#0 (f100001f -> f100003f) changes the branch decision's source.
AND x4,x1,x3 -> AND x4,x2,x3 (8a030024 -> 8a030044) changes the address selector's source.
patchword.om requires exactly one aligned old instruction in each complete raw/ELF artifact.
Native executions both retain output7 for both secrets. Full raw branch code also embeds exactly.
Address raw/ELF have different mode-specific trap tails, so they are separately bound, NOT called
translation-proved. The scanner receives the COMPLETE raw artifact including trap tail.
Arm DDI0487M.d C6.2.96 p1973 and C6.2.15 p1832 define Rn bits9:5. objdump is external oracle only.

Boundary and provenance
-----------------------
Actual omnet closure is in OMNET-AUDIT.txt and omnet-audit.json. The captured audit used mixed live
working-tree inputs at HEAD6eb49ccf, whereas SC1 base is1e075776. adapter.om differs; the captured
helpers comparison agrees. Neither entire base is qualified by that audit. Argon2i/BLAKE2b,
JSON/base64, list matching, copies, snapshot/BLAKE2s, restart, cleanup and intended bootstrap
secret disclosure remain outside this comparison's assurance. Public length/candidate count/shape
are declared by SC1 revision2, not inferred length hiding. No omnet source was changed here.

Source labels/disclosure are explicit assertions, not enforced information-flow types. The
scanner cannot authenticate target/model/disclosure metadata or authorize returned data. Dynamic
native instruction/address traces, mechanized relational proof, authenticated translation receipt,
physical constant time, cache/speculation/latency protection and service scheduling secrecy are
NOT_RUN. #759 separately owns the real envelope. No foreign first-party implementation was added.

Replay
------
Use sibling checkouts security673-relational and security673-scanner. Scanner must be immutable
PR752155ff3178f35013e405ed40d09a7dd75ede2a403. Compiler was rebuilt through existing bootstrap;
required SHA734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61.
From security673-scanner, set P=../security673-relational/seed/qualification/security673/relational,
C=../security673-relational/seed/out/omc, H=734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61.
Use fresh scratch paths and the pinned gate (or rebuild its source with C):

  nice -n19 "$P/evidence/run1/gate.bin" "$C" "$H" "$P/qualification.cases" "$P/evidence/replay-r2-qualification"
  nice -n19 "$P/evidence/run1/gate.bin" "$C" "$H" "$P/bindings.cases" "$P/evidence/replay-r2-bindings"
  nice -n19 "$P/evidence/run1/gate.bin" "$C" "$H" "$P/consumer.cases" "$P/evidence/replay-r2-consumer"
  nice -n19 "$P/evidence/run1/gate.bin" "$C" "$H" "$P/binding-negative.cases" "$P/evidence/replay-r2-negative"

Expected exits0,0,0,1. Never overwrite old scratch/evidence. No GPU, deployment, publication,
new scanner/checker authority, compiler edits or consumer edits occurred. Parent owns final review.
