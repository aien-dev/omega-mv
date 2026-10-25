# SC1-G-v3: reviewed finite append game

Frozen change contract, BEFORE source repairs. Parent approval/publication is required before executing the revised experiment. Governing SC1 revision2 SHA256: ced61decef1ff4efbf5bc81598db648aa810be434e373e9bc1563dbcb4523496. This version supersedes the incorrect v1/v2 qualification claims, not their immutable evidence. This is an offline finite model, not omnet policy or runtime correspondence.

## Historical disclosure

The old MODEL.md was amended after the first paired-product failure without a version bump: a new envelope's hidden0 became hidden1 rather than retaining0. The committed amended bytes are ad14630939342ff58c9a656c29f6e4f17ff2a6a46dd0d1ac97d833d54882dd28, preserved in git commit a485cfa1c633d2cc5fd51df24c73aa8c059b8fbc. The original f5a038d26bfa65b519038a5882cece3b7b7612a0fbf83017a5e5b1d45afdd057 bytes have now been recovered by removing exactly that transcript-recorded paragraph from the committed file and verifying the full SHA256. They are added as evidence/native-v1/MODEL.original.md, not guessed or substituted. The earlier parent message reviewed the layout but the original model was not separately committed/published before implementation. Earlier native receipts are historical, not repaired by this document.

## State and invariant

Use the explicit state/action/disturbance definitions in the preserved old MODEL.md, with ONLY the changes stated here. State encoding remains a+18*b+324*e+648*v+1296*r, BAD2592, universe2593. Clients0..17, current epoch/object bits and recovery bit retain their definitions. Capacity3; complete reservation2; durable charge1; one operation identity per client. Local invariant, nine disturbances, their order, crash/recovery and all ordinary transitions are unchanged. Epoch revoke is generic generation invalidation, NOT TKA parent-state authorization or permanent permission denial. No fairness/progress or real filesystem/power-loss behavior follows.

Explicit action correction: unsafe split dispatch (client k7 from prepared phases3..6) is enabled ONLY when the current epoch AND current object version are0. Phase16 advance remains available. Thus a split proposal cannot acquire a safe zero-cost refusing self-loop at a changed generation. Environmental revoke/substitute after action selection still makes that selected dispatch refuse unchanged, as before. This distinction is between enabled actions at selection and disturbances after selection. Quiescent action0 and empty-set refusal remain unchanged.

## Independent transition safety obligations

The transition implementation proposes a raw successor. A separate transition safety monitor observes source, selected action, disturbance and raw successor. A writing-phase client7..10 changing to renamed/uncertain11 under commit k4 is a rename event. The monitor independently recomputes the current generation after revoke/substitute and compares both expected tags encoded in the ORIGINAL writing phase. Any stale rename maps the effective successor to BAD, regardless of whether the raw successor satisfies local stock invariants. A legitimate mismatched commit refuses with its phase unchanged and creates no rename event. This monitor is part of F for fixed-point computation and an independently implemented checker obligation. G4 must actually emit a stale rename and violate this monitor; mere disagreement between two transition functions does not establish that control.

Likewise, uncertain11 becoming refunded17 is a conservative-charge violation and maps to BAD. Normal authoritative settle may move11 to12/13; partialwrite6 must not establish that authority. The checker checks the monitor obligations before ordinary transition conformance and reports concrete source/action/disturbance/raw/effective destinations. This is a finite transition-event monitor: events are inferred from explicit phase changes. It cannot observe an unmodeled hidden external effect, prove actual authorization, or qualify a real store.

Raw transition conformance against the independently enumerated specification remains a distinct check. All allowed raw transitions are checked, including rejected/unsafe proposals; an implementation fault cannot evade this obligation simply by omitting its action from the safe certificate.

## Certificate, closure and selection

Wire version3: u32 little-endian magic1396912455, version3, state-count2593, experiment-label0..6; then for every state: enabled mask, proposed safe mask, proposed public selected action (18 for excluded states), followed by nine raw successors for each enabled action in ascending order. Labels are untrusted diagnostic experiment names, NOT causes inferred from a certificate fingerprint and never admission authority.

Solver: synchronous greatest fixed point of the monitored relation. Checker: independently generated phase/action table and transitions, reverse-order losing attractor, initial inclusion, every submitted action against EVERY disturbance using proposed-region membership, and complete equality with the maximal finite safe-action relation. Remove fixed state15/16/3 fingerprint rejection. A closure failure prints its actual witness; G2/G3/G7 experiment labels may annotate this same CLOSURE assertion but cannot replace it. Source-invalid, disabled-action, absent-action, empty-disturbance and exactness failures are distinct from monitored safety failures and transition conformance.

Run the synthetic selector for EVERY winning state, not one fixture. It must use only proposed-safe bits; its selected action must be a certificate member and cannot be split dispatch/advance. The optimizer mutant changes the real selector's candidate filter from safe bits to enabled bits; it does NOT overwrite a hardcoded certificate entry. Costs remain split0, abort4, ordinary2, quiescent/recover1. Missing0/stale2 cost identity refuses; current1 is SYNTHETIC only. This is not a runtime estimate or progress proof. Every step in the serial witness must be a certificate member and every visited state retained.

## Paired certificate product

Decorate locally invariant public states with hidden occupancy1/2 if a full reservation2 exists; otherwise use0/0. New envelopes entered from0 initialize1, continuing envelopes retain their label, and release resets0. This is the disclosed explicit finite convention, not real secret allocation semantics.

Compare enabled public masks for each pair. The proposed safe mask must be contained in BOTH enabled masks. Select ONE action from the public proposed selection, independent of hidden label, and verify membership in that common safe mask. Check every proposed safe action and the SAME disturbance against both hidden labels: observations and full public successor projection equal, successor hidden labels well formed, and successors related in the proposed region. The solver's actual submitted masks/selection are consumed, not an isolated regenerated model. Hidden labels do not influence protected transition code by construction, so the product is by-construction equality plus exhaustive checking, not machine noninterference. Report distinct-hidden state/edge counts separately from identical-zero counts. The baseline mutation makes public admission depend on actual hidden occupancy rather than the reserved envelope and must fail the pair assertion.

## Source-fixed controls and frozen fixture outcomes

Each control is a separate concatenated .om source with a source-fixed fault() constant and its own executable SHA256. The positive executable has fault0 and accepts no runtime fault selector. Benchmark selection is separate. Preserve every failed build/run attempt, exact compilation command, executable/source hashes, argv, stdin corpus SHA256 and output hashes. Executable bytes may share implementation code, but source constants bind the chosen control before compilation.

G1: initial0 retained; positive monitor, conformance, closure, exactness, selection and product all pass.
G2 fault1: swapped quantifiers generate a candidate rejected by an actual CLOSURE witness.
G3 fault2: omit substitute4 in candidate safety analysis; full checker finds a CLOSURE witness.
G4 fault3: bypass original tag check, actually rename stale writing state331 via action6; independent FRESHNESS_AT_RENAME monitor fails and effective successor is BAD.
G5 fault4: partialwrite on uncertain state11/action7/disturbance6 actually refunds into17; UNCERTAIN_REFUND monitor fails.
G6 fault5: actual cheapest selector considers enabled unsafe proposals; SELECTED_NOT_SAFE fails with a concrete winning state/action.
G7 fault6: stop after one elimination pass; actual CLOSURE fails (state16 can lead to losing15).
G8: local-valid15 loses. Action6/substitute4 and action7/revoke3 reach BAD.
G9: state20 eager client1 reserve unavailable; serial trace reaches state32 where it fits, then266 with both records. Every trace action must be certified.
G10: no action or no disturbance cannot imply safety; explicit initial stutter.
G11 fault8 checker variant: hidden-dependent enabled admission fails PAIRED_PRODUCT, independent of baseline success. Counts distinguish hidden1/2 from0/0.
G12: every winning state's current synthetic choice is certified and never split; missing/stale evidence refuses.

Machine-consumed fixture constants will be in fixtures-v3.om and compiled into the checker. The old unconsumed fixtures.tsv remains historical only. Numeric state/edge/winning counts are not guessed; compare independent implementations and freeze the resulting complete corpus after execution. Formal proof, finite enumeration, native execution, differential comparison and measurements remain separate. No runtime adoption, GPU, live network, power-loss or general #647/#623 correspondence claim.
