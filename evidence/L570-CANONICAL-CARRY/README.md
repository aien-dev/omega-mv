# L570 canonical host carry, source stage only

This branch is in `aien-dev/omega-host`, not the Omega consumer worktree misleadingly named `~/workspace/omega-host`. The actual module source is `~/workspace/l396/host-src`; its exact e9fa9554 head was read back from the canonical GitHub main before making this separate worktree. Neither existing owner tree nor remote main was edited.

Original Omega a8ffd2b9, current canonical module e9fa9554 and independently reviewed prompt-source snapshot f49e6a91 are compared per source path. Only OSH source/tests/build fragments are selected; generated crumbs and unrelated compiler/driver files are excluded. Module-only changes are preserved; concurrent changes require a real three-way merge and any conflict aborts before writes. Full mapping and source-stage counts are attached.

This is NOT READY, merge approval or a qualification receipt. Existing C/OSC host scaffolding is not a seeded-Omega runtime replacement. The new exit-argument follow-on L592 and its independent review must arrive before freezing and running the final complete module/consumer gates. New test targets must be added to the module runner, not silently omitted by its historical nine-target list. Corrected publication receipts cf22be7e remain external immutable references; no earlier held packet is relabeled complete.

Canonical owner L396 is retained; parent L487 owns this uncovered carry via L570 and desk L488 owns eventual publication. No PR/issue closure, module visibility, runtime deployment, model/GPU/driver/service change, or claim of whole344 completion.
