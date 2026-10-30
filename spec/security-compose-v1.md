# Security composition profile SC1, revision 2

Status: reviewed implementation contract; qualification remains per obligation, never a blanket PASS. Revision 2 supersedes the draft hash `8bc52be7a480e279fb77673648a2f7775ba6d4841a8b1e6c427531b25d4d1860` after independent review exposed mismatched consumer assumptions. Earlier receipts retain that draft identity; they do not automatically qualify this revision.
Owner: #625 contracts; #673 integrator. Existing owner implementations and policies remain authoritative until an explicitly reviewed change lands.

## 1. Profiles, identities and trust

SC1 is additive. OM-1, OM-2, translation-v1 S1, compiler bootstrap and intentional cryptographic modular arithmetic retain their meanings. Every result binds exact source, compiler source/binary, profile, executable bytes, target/ABI, checker, model, policy and fixture. UNKNOWN, timeout, unsupported instructions, missing or mismatched bindings never become PASS.

- SC1-Q: nominal checked quantity and object-bound slice helpers. Raw construction, field writes and extraction are trusted, not sealed provenance. A qualified consumer needs a hash-bound inventory of every quantity-producing/effect site; each uses checked original operands or revalidation against the trusted extent. A helper pass is not a language-wide guarantee.
- SC1-R: pure conserved-reservation transitions. No authentication, persistence, locking or concurrent-writer guarantee follows from a transition result.
- SC1-L: static architectural leakage over-approximation for a bounded function-mode artifact using the existing owner's scanner from draft #752, pinned separately in evidence. It is not a general relational proof or a program-mode daemon scan. Binding to a daemon requires equality of the relevant emitted words plus the call/return/observation contract, otherwise BLOCKED.
- SC1-E: paired fixed-public-envelope transitions. Actual admission/scheduler enforcement remains a separate obligation.
- SC1-G: offline finite safety game, independently regenerated transitions and checked certificates. No runtime adoption or replacement verifier.
- SC1-A: omnet's existing TKA whole-service snapshot and local socket operation. A logical append is an authenticated AUM addition, not a new append-log/store implementation. Signature authentication and remote-requester authentication are different obligations.

SC1-A currently trusts the kernel, provisioned directory ancestors, same-UID/root actors, one network namespace for writer exclusion, and a same-UID adapter's asserted NodeID/admin fields. The pinned service has no SO_PEERCRED or Noise authentication evidence. Remote-principal authentication is NOT_RUN, owned by the existing adapter/Headscale integration; local fixtures prove only registry/key binding and admin enforcement. An arbitrary client's admin byte is never authentication.

Untrusted remote request bytes, ordering, cancellations and connection loss are modeled only after the trusted adapter boundary. Malicious same-UID actors, kernel compromise, storage rollback, firmware/hardware attacks and power loss are outside the current hosted qualification. All fixtures use synthetic public test keys and secrets.

## 2. Policy and effect contracts

An admitted effect must bind principal/security domain and grant; legal protocol transition and dedup disposition; checked original quantities and object identity/version; complete reservation ownership/vector; permitted observations/disclosures; and intent, linearization, acknowledgement and recovery semantics.

Correct enforcement of these rules does not prove the rules appropriate. Independent policy review must assess who may relay, sign, administer and observe; which disclosures are intended; and fairness/availability costs. Unsupported policy or environmental assumptions stay explicit.

Object domains are distinct: an in-memory view uses a trusted owner identity/version/extent; the semantic snapshot uses its exact full authority hashes and Revision; a filesystem object uses the opened descriptor's validated device/inode/type/ownership plus trusted namespace assumptions. A descriptor number alone is not identity, and an fd does not freeze contents. Quantity object handles must not truncate hashes or reuse identity/version pairs.

The new reservation profile uses nonwrapping policy epochs and nonreused operation IDs. The existing IPC v1 does NOT carry either. JSON Version is a protocol-capability version, not an epoch. Do not relabel Interactive Head as a generic policy epoch.

## 3. Checked quantities (#756)

Unsigned scalar words retain modulo arithmetic. `qty` contains value and cap. Statuses: 0 success; 1 malformed operand; 2 domain mismatch/negative signed ingress/widening through narrow; 3 capacity overflow or underflow; 4 object/version mismatch. Refusal leaves the complete output unchanged. Compare signed ingress against zero BEFORE unsigned conversion; conversion itself may trap.

Add validates a,b and checks `b <= cap-a` before adding. Multiply checks `b <= cap/a` when a is nonzero. Subtract checks b<=a. Narrow checks target cap and value. Different caps refuse rather than guessing a result bound; equal caps do not distinguish physical units. Epoch increment refuses maximum. Slice formation validates exact identity/version/extent, then offset<=extent and count<=extent-offset before effects.

A small wrapped value cannot establish a valid original length: `qword(add raw_a raw_b)` is not checked addition. Serialized slices require reconstruction of checked original offset/count and comparison to the trusted current object immediately before use. Distinct output/input structs follow the seed alias rule.

| ID | Frozen outcome |
|---|---|
| Q1 | `(2^64-8)+16` modular baseline8; checked add3, output unchanged |
| Q2 | Exhaustive widths1..8 against exact wider signed arithmetic AND external arbitrary-precision oracle over emitted actual outcomes; finite, not u64 proof |
| Q3 | u64 extrema, malformed1, unequal caps2, zero/exact capacity0 |
| Q4 | max*2, `(2^32)^2`, `(2^32+1)^2` refuse3; `(2^32-1)^2` and zero*max succeed0 |
| Q5 | Negative signed ingress2 before conversion |
| Q6 | Narrow256 to255 refuses3;255 succeeds; qnarrow with target cap above source cap refuses2 |
| Q7 | Exact/empty slice0; extent violation3; stale object/version4 |
| Q8 | Scalar/qty, object/qty and slice/qty mix compile refusal7; intentional crypto wrap accepted |
| Q9 | Epoch maximum refuses3, no wrap/rotation |
| Q10 | Actual omnet original frame length/cursor migration and authority/budget original-operand migration; inventory remaining consumers |
| Q11 | Compiled removed guards and modular-instead-of-checked lowering must reach intended assertions; #623 native correspondence separately bound |

## 4. Resource conservation and secrecy (#757/#759)

Each stock dimension obeys nonnegative U,R,B and `U+R<=B`, checked by guarded subtraction. Stocks: processes/descendants, descriptors, resident memory, queued bytes, persistent bytes, temporary snapshot bytes, retained history/logs and cleanup reserve. Charge shared physical parents once. The pure helper has two fixed owners, one attributed slot each, no delegation API and no unlimited-demand availability claim. Sibling-sum enforcement/delegation remains the authority/scheduler owner's obligation; copying capacity never delegates it.

The complete vector reserves atomically in ONE authoritative writer or does not change. The omnet writer is its descriptor-held abstract Unix lock plus single-threaded `dsrequest` dispatch. The old Omega PID-lock dispatcher is not this writer and remains BLOCKED on stale-reclaim ABA, generation timing, ignored release errors and missing directory-sync semantics.

Bind reservation ID, exact request/principal/grant/object/version/epoch. Unknown cancellation, death, timeout, delayed completion or lost acknowledgement retains full charge. Commit actual<=reserved and refund slack only with authoritative quiescence and observation permission. Reclaim persistent stock only after actual reclamation and required durability. The pure helper spends fuel monotonically without refund; this is NOT durable fuel enforcement in the current daemon.

Integration must classify every stock as durable or volatile. Volatile release at crash/rollover is permitted only for proved effect-free work, with that event declared a public boundary. Durable identity, envelope and fuel cannot be reconstructed as fresh zero after restart. A per-epoch replenishment policy is possible only as an explicit different policy, not an implicit reset.

Snapshot creation needs a standing maximum temporary-snapshot reservation plus cleanup reserve BEFORE staging. The snapshot cannot bootstrap the durable reservation that pays for its own initial write. The current v1 snapshot has no resource frame; its reserved metadata words are checked zero. The owner must choose derived authoritative accounting plus standing envelopes or a versioned schema/migration, and prove the physical maximum from actual framing. No second ledger/store is introduced.

Recovery reconciles retained committed and temporary physical state internally. Public capacity is computed only from public envelope maxima, never secret actual usage; internally check actual<=envelope. Each secret frame is charged its public bound (currently1024), or padded under a separately implemented policy. R4's40 ledger/60 retained committed/cap100 must refuse new60. If the extra20 is an effect-free stale temporary file, authoritative cleanup may allow later admission; that is a different fixture, not evidence for retained committed accounting.

SC1-E reserves a fixed public partition before secret work. Completion, cancel, errors and supervisor work cannot change public admission, ordering or reclamation. Release requires an exact public release identity AND authoritative quiescence. Unknown retains; secret-dependent delay is not certified noninterference. Across restart, durable public envelope identities must be restored before public admission. The real tkad `dsbatch` serial dispatch loop is the candidate admission point, but is NOT integrated: elapsed readiness exposes service completion. Untimed accept/refuse and order are distinguished from timing; fixed release scheduling is a separate missing owner capability.

| ID | Frozen outcome |
|---|---|
| R1 | Capacity6, two requests4: at most one atomic success |
| R2 | Vector atomic rollback, binding/replay refusal, actual<=q, attributable reclaim, fuel not refunded |
| R3 | State(u,r1,r2), cap3, idle reserves1..2, commits0..q, release1, whole-service effect-free crash clears both:18 states78 deduplicated edges; explicit edge corpus/hash required |
| R4 | Ledger40, retained physical60, cap100: new60 refused; no smaller-ledger recovery |
| R5 | Real concurrent writer schedule, crash/rollover accounting and standing temp reserve: BLOCKED until actual integration |
| E1 | Capacity2, secret occupancy1/2 makes public request1 differ in baseline; fixed envelope observations equal |
| E2-E5 | Completion/cancel/error/supervisor refund mutants must execute and fail paired observations |
| E6 | Public release plus quiescence, recovered envelope identity; actual scheduler correspondence remains BLOCKED |

R3 is effect-free only. Its crash release rule never applies to uncertain external or disk effects. Per-client crash and no crash each give70 edges; counts alone are insufficient.

## 5. Real append, identity and recovery (#670/#659/omnet#3)

The current TKA policy authenticates AUM signer keys against the AUM's retained parent state, then applies upstream fork choice. A registered NodeID is a relay, not necessarily a signing key. An AUM arriving after a head RemoveKey may validly refer to an older parent; pinned upstream oracle expectations decide this case. Do not replace it with a current-head-only rule. Current-state checks apply to node signing/registry/disable/finish according to their own contracts. Registry/key/admin checks occur in the same single-writer request interval; prepared permissions cannot escape that interval.

Candidate preparation includes bounded response encoding before persistence. Store ordering is temp write, file fsync, framing/checksum validation, rename, directory fsync, staged-descriptor close, live-slot publication, acknowledgement. **Linearization is successful rename.** Acknowledgement requires successful directory fsync and close; close failure after fsync still causes exit74/UNKNOWN. The pre-rename validation is not a semantic reload. Restart's semantic validation may refuse a checksum-valid invalid candidate; that fail-closed refusal is the current outcome, not silent initialization.

After rename with sync uncertainty, or failed cleanup, exit74 without further admission or rollback rename. The client outcome is UNKNOWN; a recovered validated snapshot may contain the effect even without an acknowledgement. Recovery is qualified only for process crashes. A later successful fsync does not prove power-loss durability after prior writeback EIO; Linux may report an error only once. Current stale-temp unlink lacks directory fsync. Neither is a power-loss receipt.

| Route | Identity / retry contract at pinned base |
|---|---|
| sync/send | Full canonical AUMHash, including signatures; alternate wire encodings with same canonical identity are accepted no-ops. Interactive Head must equal resulting chosen head. Base rewrites Revision/snapshot even on no-op; proposed fix must suppress it and be qualified |
| init/begin, init/finish, disable | State-dependent retry may return409 after a completed transition; no generic operation-id dedup exists |
| sign | Current-authority verification; successful retry may return200 with snapshot rewrite |
| registry/map | Trusted adapter policy, exact registered node binding; route-specific state outcomes |

Disposition of omnet#3's generic replay-refusal row: upstream canonical AUM replay is accepted no-op; this is an explicit compatibility constraint, not a weakened test. Same operation ID with different bytes can be refused only after a new versioned IPC operation-ID contract exists. It is BLOCKED, not claimed for v1.

| ID | Frozen consumer fixture / result class |
|---|---|
| A1 | Two real local socket clients append signed AUMs; signature authentication verified separately from adapter assertion trust |
| A2 | Wrong registry NodeID/NodeKey/admin/action and invalid AUM signatures; outcomes from route policy/upstream oracle |
| A3 | Interactive Head mismatch409; disable stale Head409; sign after key removal403. Generic epoch/version request fields unsupported |
| A4 | Truncated/trailing/oversized/malformed/wrapped ingress; close or bounded error before effects as documented |
| A5 | Deterministic serialized request/revocation order; not mid-dispatch concurrency in the serial daemon |
| A6 | Final-component symlink, directory replacement after open, swapped resume descriptors; trusted same-UID interference assumptions retained |
| A7 | Competing full reservations/cancel: real-service integration BLOCKED, helper results separate |
| A8 | Supported short-write/ENOSPC/pre-rename cleanup, rename then directory-fsync EIO; syscall-wrapper variants bound as distinct executables |
| A9 | Process restart/lost acknowledgement and independent persisted-state inspection; canonical no-op replay increments Revision at base, MUST leave Revision and snapshot unchanged after the proposed suppression fix |
| A10 | Paired real-service secret inputs/output policy and matched workload measurements; not timing noninterference |

Public outcome vocabulary includes400/403/404/409/413/500/503 with fixed bounded error bodies; malformed transport may close without reply. Store fail-stop74, loop failure75 and exec/resume failure76 are separate outcomes, not successful effect receipts. Fixtures must name the exact intended route/result, not accept arbitrary failure.

A persisted intent does not atomically authorize a later process/network/device effect. Such effects require real provider fencing, idempotency and authoritative reconciliation. Post-effect generation checks cannot undo unauthorized execution.

## 6. Secret/public contract (#758)

For the public-signature path, AUMs, keys, signatures and validity are public; variable-time verification is permitted by this contract. For the secret path, disablement/support-secret contents and derived KDF values are secret before authorized disclosure. Request length and public state/candidate count/shape are public in this bounded profile; a length-hiding profile requires padding and new evidence.

Observers are requester/public response status and body, Revision/Disabled, connection close, untimed admission/order, and the SC1-L architectural trace. Validity may be disclosed to the authorized requester at the caller's final decision. Successful disable intentionally discloses Disabled/Revision and the disablement secret via bootstrap, per upstream policy. Candidate index and mismatch-prefix length are NOT disclosures. REQUIRED owner change: comparison scans all32 bytes and candidate matching scans the complete public list. Both exit early at base1e075776; L2/A10 do not become verified until the actual proposal is independently checked. The scanner has no general declassification facility: the authorized output/caller decision sits outside the scanned function graph.

SupportDisablement confidentiality means no route/error emits it and at-rest access relies on0600/private-directory same-UID trust. It is plaintext in the snapshot. There is no zeroization, remanence or crash-dump protection claim.

The complete dependency closure includes raw JSON parse, base64/byte decoding, copies, Argon2i/BLAKE2b, hash/list comparisons, finish support checking, snapshot framing/BLAKE2s and variable-size writes, recovery rechecks, stack/16MiB pool lifetime, stale-temp unlink and crash dumps. A bounded comparison scan does not qualify that closure. Caches, speculation, instruction latency, OS timing and physical observations are outside SC1-L. Secret-dependent UDIV/SDIV MUST be refused in the stronger profile, but the current scanner only propagates their taint; constant-divisor secret dividends may pass. L4 division is BLOCKED pending the existing scanner owner's reviewed extension or explicit division-free artifact admission. A secret divisor's trap rejection does not establish this stronger property.

| ID | Required fixture / obligation |
|---|---|
| L1 | Nondeterministic source output{0,1} refined to secret: ordinary inclusion holds, relational secrecy fails; paired native .om declared-output checker, not the static taint scanner |
| L2 | Bounded compare static scan, fixed public shape and result-only disclosure |
| L3 | Actual emitted branch/address mutants preserve ordinary outputs but trigger leakage refusal |
| L4 | Secret division, helper-call and trap paths refused or explicitly unsupported |
| L5 | Wrong target/bytes/model/policy/checker invalidates claim; scanner API gaps BLOCKED under #623/#624 |
| L6 | Function-mode to real program-mode emitted-word correspondence; absent correspondence BLOCKED |
| L7 | Actual omnet secret path paired outputs, complete closure classification, remaining unverified sites inventoried |

## 7. Offline safety game (#760)

Freeze finite state/action/disturbance encoding before implementation. Include protocol/current+expected epoch/object, both reservations, persistence/ack/recovery, concurrent request, revoke, substitute, partial write, crash, delay and lost ack. Unsupported disturbances/bounds remain explicit. Start with invariant states; iterate greatest fixed point `exists action, forall disturbance, successor in K`. Empty choices cannot imply safety; quiescence is explicit stutter. Independently regenerated transitions and losing-attractor enumeration check certificate exactness/maximality for this finite model only.

Paired secrecy uses ONE action selected from the pair's common public state and intersection of enabled sets, the SAME public disturbance, equal permitted observations and related successors. Do not let the certificate select by hidden state. A fixed-envelope hidden-occupancy extension may establish finite equality by construction plus exhaustive checking; it is not runtime correspondence.

G1 positive certificate; G2 swapped quantifiers; G3 omitted disturbance; G4 stale epoch; G5 uncertain refund; G6 cheapest unsafe proposal; G7 early iteration stop; G8 locally invariant losing state with witnesses; G9 legal serial schedule versus unsafe overlap; G10 no empty-set vacuity; G11 paired-product observations plus secret-admission mutant; G12 missing/stale cost refuses. Every mutant compiles and normally fails the intended assertion. Cost ranks only proven-safe actions. Synthetic costs are not #583 estimates. General #647 analysis integration and runtime adoption require independently checked correspondence, otherwise BLOCKED.

## 8. Evidence and acceptance

Independent reviewers assess fixture expectations, source and correspondence separately. A mutant counts only after successful compile and normal execution to its intended failed assertion; signal/build failure/timeout is INVALID unless testing that exact refusal. Retain bad attempts and no-win results.

Preserve seed fixed point, nominal calls, cryptographic vectors and release gates. Reuse existing gate669 and owner OM-2 scanner rather than replacing them; gate669 is Omega-only. Existing external development oracles remain external. All new first-party implementation, fixtures and runner logic is .om.

Report formal proof, finite models, differential tests, native/emulated execution, host measurements, process crashes and power-loss qualification separately. Bind fault variants to their own executable bytes. Clean-checkout replay and final independent review follow coherent changes. Measure matched throughput, p50/p95/p99, resources and verification overhead with raw samples; a two-sample append measurement cannot support tail quantiles. Timing is regression evidence, never a secrecy proof.

The #673 acceptance matrix must include every Q/R/E/A/L/G obligation, status VERIFIED/FAILED/BLOCKED/NOT_RUN, assumptions, exact evidence and responsible owner/next step. Policy appropriateness has its own review row. No parent issue closes from helper/model results, and no merge/deployment gate is bypassed.
