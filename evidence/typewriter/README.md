# evidence/typewriter (ledger L308, typewriter experts)

Every number in docs/speed/TYPEWRITER_EXPERTS.md comes from a file here. Model: Qwen3.6-35B-A3B-FP8 rev 95a723d0
(40 layers, 256 experts, top-8) unless a file name says 30b. Tool: `typewriter` in aien-sovereign-core
feat/typewriter-experts (crates/aien-inference-abi/src/bin/typewriter.rs); binary hashes in each `*.sha256`.

| File | What | Made by |
|---|---|---|
| texts/ | 4 domains x (fit, held-out): different documents, see texts/SOURCES.md | hand-picked, sources below |
| routes/*.ids.json.gz | per layer, per token, the 8 routed experts (ids), nll per token; weights dropped to save space | run-routes.sh (host reference, CPU) |
| heads-0.90.json, heads-0.99.json | H1: experts per layer for 90/99 % coverage, overlap, own vs general vs random head on held-out, the heads | `typewriter heads` |
| placement-plan-35b-*.json | the model ripped apart by structure, bytes per part, tier fit, beat bytes | `typewriter plan` with tiers.json |
| tiers.json | measured tier sizes and read rates (reused from omega doc/machine-ceiling@3c8ce239, L151) | copied with sources |
| trace-35b.json | H4 prefetch hit rates and task-switch delay; H6 expert union vs N sequences | `typewriter trace` |
| load/ | H2: NVMe and memory timing of one head | run-load.sh under quiet-measure |
| masked/ | H5: held-out scored with routing restricted to a head (OUTPUT DIFFERS BY DESIGN) | run-masked.sh |
| plan-review-astra.txt | outside review of the plan | codex gpt-6-astra |
