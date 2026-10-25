# SC1 dependency and ownership map

Programme #673; source reconciliation starts at Omega main
`00eb0e7106d2f8fc841e5e16eb624fa1bd029111`. Main lacks the staged authority
and native/control paths. Never infer a merged production owner from a draft.

| Contract/surface | Existing owner | Exclusive proposal writer / dependency |
|---|---|---|
| #625 contracts | ORPHAN semantics coordinator | TURING integrates SC1 spec and acceptance; independent contract review corrected draft mismatches |
| #756 quantities, #636 compiler | ORPHAN/CompilerLane | QuantityLane owns om/quantity only; no compiler semantic change; TURING integrates actual consumer build |
| #670 authority, #659/#649 recovery | Existing authority stack | TURING focused cleanup compile repair on #688; production fencing/ABA/durable epoch remain owner defects, not helper claims |
| #757 reservations | Authority/store writer | BudgetLane owns new pure reserve.om/envelope.om; actual omnet snapshot schema/standing reservation remains WORM decision |
| #758/#623 secrecy/validation | ORPHAN translation/checker owner | SecrecyLane owns new relational fixtures; pinned #752 scanner reused; target/model/disclosure and whole-consumer correspondence remain checker owner |
| #759 public envelopes | Existing scheduler/admission owner | Pure paired envelope helper only; no qualified .om scheduler seam, actual dsbatch scheduling/capacity enforcement BLOCKED |
| #760/#647 analysis | Existing analysis owner | GameLane owns bounded offline game/certificate experiment, no general verifier replacement |
| #583 cost model | Existing cost-model owner | Synthetic ranking only until qualified real cost evidence; no runtime admission |
| #671 ingress | Existing parser/omnet owners | ConsumerQuantity migrates real socket original-length/cursor sites; remaining parser sites inventoried, not silently qualified |
| omnet #3 | WORM TKA/store/daemon/Headscale | Isolated proposal at PR#4 immutable base; ConsumerQuantity writes socket.om/rpc.om, ConsumerSecret writes helpers.om, qualifier only qualification/security3/; WORM live tree untouched |
| #624 qualification | ORPHAN qualification coordinator | TURING integrates exact evidence, independent reviews, clean replay and matrix; no gate/registry policy edit |

TURING is the sole build/registry/compiler integration writer for these proposals.
No compiler or registry change is currently made. The consumer build change adds
one existing-source composition input; new implementation and fixtures are .om.
Workers never share writable source files. Existing owner branches remain theirs;
proposal merges require normal owner review and approval gates.

## Reconciled source inputs

- Authority base `a35697e94b5eeb72efadfee6edab7f14344aea43`.
- #685 `346c9ad3d171a19ea3f015773a41ceeb3feac8f1`: acquire-before-read repair.
- #686 `c0b9a84b27a904dbd23af9dae0b95a5f198e46c8`: exact record/generation EOF consumption.
- #687 `95529421e9a0e6d6d63ca5d8b8cdfaf443e0ef78`: unlink-error reporting, NOT stale-pid ABA repair.
- #688 `08bd7aa7d7b73864e56163772ccceaa9cc91de0b`: write-failure cleanup; its cleanup gate failed pinned compilation, corrected by focused #769 without production behavior changes.
- Existing signed M19/R11/R13/operator failures remain historical failures; four native fixtures do not erase them or qualify the full coordinator.
- omnet #2 is actual TKA/model/signature code, not the obsolete AGENTS-only tracker snapshot. #4 at `1e075776855d4389b69b23b54b49ed230d488717` adds the existing daemon and snapshot implementation. Current active Headscale work is not an immutable dependency and is not edited here.
- OM-2 compiler work #750/#753/#754/#764 and translation #755 remain separate owner branches. Scanner input is #752 at `155ff3178f35013e405ed40d09a7dd75ede2a403`; emitted evidence pins it, not a moving checkout.
- Current consumer compiler `734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61` must not be replaced by authority stack's older `66b4cf34e5cd2c8ff003bd142e7309f52178b66c2380f78ac313b6eb8b67471b`: the old exec operation closes inherited descriptors needed by tkad rollover.

## Coordination records

- [#673 original map](https://github.com/aien-dev/omega-mv/issues/673#issuecomment-6402294156)
- [#625 contract discussion](https://github.com/aien-dev/omega-mv/issues/625#issuecomment-6402400478)
- [#636 compiler seam request](https://github.com/aien-dev/omega-mv/issues/636#issuecomment-6402300850)
- [omnet owner seam](https://github.com/aien-dev/omnet/issues/3#issuecomment-6402300478)
- [Concrete no-op/accounting/quantity handoff](https://github.com/aien-dev/omnet/issues/3#issuecomment-6402476068)
- [#4 integration compatibility warning](https://github.com/aien-dev/omnet/pull/4#issuecomment-6402415511)

No issue closure, ready-to-merge label, merge override, deployment, live service
restart or GPU operation is authorized by this evidence. Formal policy correctness,
remote identity authentication, durable aggregate resource admission, scheduler
noninterference, complete emitted-secret closure and power loss are separate rows,
not conclusions inferred from component results.
