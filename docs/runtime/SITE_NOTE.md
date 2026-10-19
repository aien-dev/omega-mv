# Note for www.aienos.com: Omega

Draft for Drake's review, 2026-10-19. For the aienos.com site builder (`aien-dev/aienos.com`,
`crates/site-builder`) to place as a card or short page. Same words as the Omega README. Every
figure links to its receipt; no comparisons with other people's systems.

---

**Omega: an open-source alternative to CUDA, for anyone in the world.**

Omega drives an NVIDIA GPU with our own code from top to bottom: our own way to write kernels, our
own machine-code encoder, our own launch path and our own libraries. It needs no NVIDIA user-space
software: no libcuda, no cuBLAS, no nvcc. It uses only what the hardware requires, the open-source
kernel driver and its firmware. License AGPL-3.0.

Pre-alpha. It runs today on the NVIDIA DGX Spark (GB10).

- Kernels written in Omega run on the GB10: matrix multiply, attention, reductions, elementwise.
  Receipts: `omega-mv/evidence/`.
- No CUDA library on the path. Receipt: physics `evidence/m16-blackwell-native-path-requalification-receipt.json`.
- A real model runs end to end on it: Qwen3-30B-A3B. Receipt: aien-sovereign-core
  `docs/inference/evidence/own30b-omega4799-20261015/`.
- Measured against the machine's own limit: 115.14 TFLOPS on the tensor cores, 195.1 GB/s of
  memory reads. Receipt: `omega-mv/evidence/OWN30B-T3-TC-PEAK-BW-5faa0f70-20261013T2236Z/`.

Not yet: a versioned API specification, a first-kernel example for newcomers, several processes on
one GPU, general any-shape libraries, any GPU other than the GB10.

Read the source: https://github.com/aien-dev/omega-mv. Read the plan: `docs/runtime/ROADMAP.md`.
