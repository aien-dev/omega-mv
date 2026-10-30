#758 relational comparison slice, SC1 revision2

Current contract: ced61decef1ff4efbf5bc81598db648aa810be434e373e9bc1563dbcb4523496.
Current records: CURRENT-BINDINGS.json (run4), evidence/run5/RUN5.txt (review fixes), FROZEN-R2.txt,
SC1-L.txt, SHA256SUMS.
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
   secret0/1 but are refused by the scanner. Which scanner verdict path each control proves:
   - branch, helper, error, division, early, public-branch-mut: the data-branch refusal
     (VERDICT NOT_RUN, exit2, SUMMARY data-branch>=1).
   - address, public-address-mut: ALSO only the data-branch refusal. omc guards a secret index with a
     bounds-check b.cs (BRANCH @3 b.cs data), so exit2 comes from that branch, not from the load.
   - address-nobounds (run5): the same address image with that b.cs (word3, 540000c2) patched to
     NOP d503201f by patchword.om. No branch remains; the secret-indexed load (MEM @4 data-index)
     alone yields VERDICT FAIL, exit1. This is the only control of the data-dependent-address
     FAIL path; a scanner that never returns FAIL for data-index loads fails scan-address-nobounds.
   No crash/build failure/UNKNOWN counts as a kill.
L4 BLOCKED stronger division policy: current scanner PASSES dividend.om (secret x0 divided by
   constant7, then xor(q,q) returns0). SC1 revision2 forbids secret-dependent UDIV/SDIV, so that
   artifact is UNQUALIFIED despite scanner PASS. The existing scanner owner must add reviewed
   enforcement or approve explicit division-free admission. Secret divisor zero-check, helper
   and explicit trap controls do produce DATA refusals, but do NOT establish division policy.
   The scanner PASS on dividend.bin is a separately reported tripwire (l4-tripwire.cases,
   "SECURITY758 TRIPWIRE L4-division-gap"), not part of any PASS total below.
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
run4 (historical, CURRENT-BINDINGS.json): qualification.cases56/56 (then including the L4 scan),
bindings.cases16/16, consumer.cases4/4, binding-negative.cases exit1 with2 FAIL rows.
Logs: evidence/run4/{qualification,bindings,consumer,wrongbindings}.
run5 (review fixes, evidence/run5/RUN5.txt): L4 moved out of qualification.cases into
l4-tripwire.cases; derive-address-nobounds and scan-address-nobounds added. qualification.cases
56/56, bindings.cases16/16, consumer.cases4/4, binding-negative.cases exit1 with exactly the
wrong-source-bytes and wrong-checker-bytes FAIL rows; l4-tripwire.cases2/2 reported separately.
These gate PASS counts include expected scanner refusals and FAIL verdicts. They are not blanket
SC1-L or service PASS counts.

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
One omega-mv checkout, from the repository root (AArch64 Linux, no GPU). The automatic form is the
local gate tests/gates/omega-security758.gate:

  bash tests/gates/run.sh security758

It checks seed/out/omc against seed/out/omc.sha256, rebuilds the OM-2 case gate from
seed/qualification/om2/gate.om with that compiler (sha256
6c8691c21074595312fdd0f22ca30b6fbe48fb88f2fb0a58936f2960056acefa, byte-identical to the committed
evidence/run1/gate.bin), runs the five case files below and requires exits 0,0,0,1,0 with exactly
the two intended binding-negative FAIL rows. The scanner is seed/qualification/om2/leak/leakscan.om
on main: introduced by 725ca83978a7e547e354011fc1f9ce3c84958592 (#752) and unchanged since; its blob
da9268a0c835e056ddb5ddc39f570bcbc741089a is identical to the earlier pin
155ff3178f35013e405ed40d09a7dd75ede2a403 (and gate.om blob 38db128f21511f18ca3b1d26b90f357de0c436e2
likewise). The checker-pin case binds its compiled bytes (aa60061e...e633). By hand:

  P=seed/qualification/security673/relational C=seed/out/omc H=$(cut -c1-64 seed/out/omc.sha256)
  "$C" < seed/qualification/om2/gate.om > /tmp/s758/gate.bin && chmod 700 /tmp/s758/gate.bin
  for c in qualification bindings consumer binding-negative l4-tripwire; do
    /tmp/s758/gate.bin "$C" "$H" "$P/$c.cases" /tmp/s758/$c; echo "$c exit=$?"; done

(/tmp/s758 must exist and each scratch directory must not.) Expected exits 0,0,0,1,0. All case paths
are relative to the repository root. The suite was first bound to compiler 734f0a39...6b61 at base
00eb0e7106d2; MAIN-REPLAY.txt records that the current seed/out/omc 62775d2a...712b produces the same
results and byte-identical ELFs. Do not use the unrelated seed/qualification/security673/gate669.om
conductor. Never overwrite old scratch/evidence. No GPU, deployment, new scanner/checker authority,
compiler edits or consumer edits occurred.

Independent final review/replay
RelationalReview rechecked sourceacf6cecc24a2f691a9cbebea3bac77ea009f25e5 and found no
remaining actionable findings within bounded comparison/static-evidence scope. A later review
(#774, Rev774) found that no case exercised the data-dependent-address FAIL verdict (L3 above); run5
adds that control and the reviewer's mutant now fails scan-address-nobounds (evidence/run5/RUN5.txt).
clean-replay.json records a fresh sibling-checkout bootstrap and rebuilt existing
OM2 case runner:56/56,16/16,4/4 and exactly two expected binding-negative FAIL rows.
The parent's initial invocation of the unrelated gate669 API refused90; that setup
error is retained and does not count as qualification. No source fix was needed.
L4/L5/L6/L7 owner blockers above remain unchanged. Historical run4 documentation
bindings refer to acf6cecc, not this later review/replay note.
