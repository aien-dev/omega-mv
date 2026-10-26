# Remaining C → Om inventory

Snapshot: `aien-dev/omega-mv` main **d863780eb42bd940349eba61a3d1c78b0b819941**, inspected 2026-10-24. [Exact-path CSV](C-TO-OM-INVENTORY.csv).

This is an inventory and cutover backlog, **not a retirement or deployment claim**. The main tree contains **674 `.c` + 331 `.h` = 1,005 tracked paths**, and no tracked `.om`. Candidate branches already contain seeded Om; main alone does not describe the whole programme. Counts cover this repository, not external PHYSICS/NVIDIA dependencies, other extensions, assembly or shell/Python automation.

## Role triage

| Role | `.c` | `.h` | Total |
|---|---:|---:|---:|
| Active-source candidates under `src/`, excluding explicit cases below | 268 | 250 | 518 |
| Tools, tests, benchmarks, research and calibration | 339 | 50 | 389 |
| Explicit reference/model code | 4 | 3 | 7 |
| Historical evidence copies and audit baseline | 63 | 27 | 90 |
| Confirmed generated header | 0 | 1 | 1 |
| **Total** | **674** | **331** | **1,005** |

These categories are conservative **triage**, not complete per-file production reachability. A file under `src/` is not automatically deployed; a tool can be a production consumer. Conversely, absence from a literal Makefile search does not prove a file unused: variable expansion, wildcards, scripts, includes and external consumers matter. The CSV records literal references in `Makefile` and `mk/*.mk`, not a fully expanded dependency graph. No production or hardware tests were run for this inventory.

### What the build establishes

- Root `Makefile` defaults to `all`, which builds the C `omegatool`; `SRCS` explicitly includes core, codec, realization, CPU/GPU code, `tools/omegatool.c`, and external PHYSICS `m16_native.c`/`nvrm.c`. Active C is not merely a Linguist artefact.
- `-include mk/*.mk` adds independent compiler, checker, runtime, tensor, tool and test targets. `mk/compiler.mk` builds `src/compiler/*.c` through `OSC_LIB`; its model and model-generator sources are independent test machinery. “gen” in `osc_model_gen.c` means a handwritten sequence generator, **not generated source**.
- `src/runtime/rx_seq_reference.{c,h}` explicitly identifies the R15 reference oracle, with Makefile R15 consumers. `src/turing/field_select_v0_retired.c` reproduces the recorded kill-test failure and remains in `TURING_SRCS`. These are active reference dependencies, not removable merely because their names say reference/retired.
- Only `src/osh/osh_layout.h` is classified generated here: its header names `src/osh/osh_gen.sh` and `osh_layout.lst`, and `mk/osh.mk` wires regeneration/checking. Generation does not make its active ABI dependency disappear. `src/omega_numeric_transc_tables.h` is frozen shared constants moved verbatim from source; it is **not** classified generated.
- **89 paths under `evidence/` are immutable historical evidence and excluded from this deletion backlog.** The additional historical path is `docs/osc/audit/baseline/shim.c`. Preserve original evidence bytes/provenance; do not alter historical receipts to improve language percentages. No blanket generated/vendor classification is applied, and no tracked vendored C was established by this inspection.

The 389 support paths break down as: tests 273, tools 61, research 47, bench 7, calibration 1. A replacement must include first-party tests/build automation too; this grouping is not an exemption from the destination-language policy.

## Subsystems and existing work

Counts below include both C source and headers, before role exclusions. They locate work; issue links identify existing coordination, not newly assigned ownership.

| Subsystem | Paths | Existing coordination / consumer gate |
|---|---:|---|
| `src/` root: core, CPU/GPU, numeric, hashing | 195 | [#599 programme](https://github.com/aien-dev/omega-mv/issues/599), [#638 seeded CPU→GPU](https://github.com/aien-dev/omega-mv/issues/638), [#661 end-to-end model](https://github.com/aien-dev/omega-mv/issues/661); retain CUDApp canonical runtime ownership and live-service boundaries |
| `src/runtime` | 65 | [#651 reuse lifecycle](https://github.com/aien-dev/omega-mv/issues/651), [#649 lifecycle](https://github.com/aien-dev/omega-mv/issues/649), [#659 recovery](https://github.com/aien-dev/omega-mv/issues/659); durable identities, restart/revocation and single authoritative writer must survive cutover |
| `src/compiler` | 31 | [#621 source-aware Om](https://github.com/aien-dev/omega-mv/issues/621), [#650 compiler correspondence](https://github.com/aien-dev/omega-mv/issues/650); inspect existing seeded compiler, do not build another |
| `src/checker2` | 11 | [#623 checked lowering](https://github.com/aien-dev/omega-mv/issues/623), [#647 verifier worklist](https://github.com/aien-dev/omega-mv/issues/647); preserve receipt trust boundary and refusal behavior |
| `src/tensor`, `src/autodiff`, `src/train` | 8 + 2 + 4 | [#663 tensor](https://github.com/aien-dev/omega-mv/issues/663), [#664 autodiff](https://github.com/aien-dev/omega-mv/issues/664), [#665 graph/compiler](https://github.com/aien-dev/omega-mv/issues/665); qualify actual inference/training consumers |
| `src/searchtrace` | 11 | [#607 tracing](https://github.com/aien-dev/omega-mv/issues/607), [#608 archive](https://github.com/aien-dev/omega-mv/issues/608); observational trace archive must not become Cortex's authoritative writer |
| `src/osh`, `src/visor`, `src/language` | 23 + 21 + 6 | [#635 native command retirement](https://github.com/aien-dev/omega-mv/issues/635), [#637 attachment](https://github.com/aien-dev/omega-mv/issues/637), [#667 tooling](https://github.com/aien-dev/omega-mv/issues/667); preserve external command/stream/error contracts |
| `src/physics0`, `src/turing` | 36 + 35 | [#599](https://github.com/aien-dev/omega-mv/issues/599), [#522 measured realization](https://github.com/aien-dev/omega-mv/issues/522); retain measurement validity and sealed evidence |
| Remaining `src/` families | 78 | algebra 19, estimation 10, polyglot 9, crumbline 8, dual 8, fabric 8, dual_experiment 5, reservoir 4, ans 3, allen 2, path 2; reconcile exact owner through #599 before implementing |

## Small bounded cutover candidates—not claims of readiness

1. **One hashing consumer, not the global hash ABI.** `migration/osc-to-seeded-omega` at `9b072eb492e106d23f7eed984c5645251601adee` contains `seed/native/sha256.om` and `tests/seed-migration/sha256.py`, with boundary/fragmentation/closed-input cases. The Om program accepts stdin up to 8 MiB and emits hex after EOF. Current `src/sha256.{c,h}` exposes incremental `sha256_init/update/final` used throughout the C tree. First select one compatible byte-stream consumer; qualify exact framing, size/refusal/exit behavior, seed/binary provenance and independent controls; switch that consumer and remove only its superseded implementation/dependency. This CLI is **not ABI-compatible** and does not authorize deleting shared SHA C. Reuse #635/#667 and the compiler owner.
2. **One legacy compiler command consumer.** Existing successor source is `seed/omc/omc.om` on `seed/omega-in-omega` at `57f5f4258cebf3d5b2c22853cd062e351265c525`, also present on the inspected migration branch. Map a specific caller of `src/compiler/oscc_main.c` before changing it. `.osc` and `.om` are not interchangeable semantic profiles; migrate source and diagnostics/receipt expectations explicitly, run #621/#623/#624 conformance and #650 provenance checks, then remove the now-unused C entry point/dependencies. The inspected migration branch still contains the legacy compiler and C default build; branch presence alone is not cutover evidence.
3. **Reference-test retirement only with an Om test successor.** `src/runtime/rx_seq_reference.{c,h}` and the frozen Turing selector have narrow named consumers, but this inspection did **not** establish qualified Om replacements. Preserve failure receipts; replace and qualify the test/oracle behavior before removing active source. Do not claim these are safe deletions today.

The inspected `seed/retirement-integration` branch at `12251e3403d42931ada27dc1c54b3b610954be5d` contains `seed/retirement/trn1/trn1.om`, but its own `docs/retirement/README.md` explicitly reports **zero verified production retirements**: the framed native utility does not migrate the original Rust library ABI/callers. It is useful qualification precedent, not proof that remaining C has already been replaced.

## Acceptance for each implementation PR

Link the existing issue/owner, name exact C paths and Om successor pins, inventory all callers/build dependencies, qualify equivalence and negative controls, switch real consumers, remove superseded active implementation and obsolete dependencies, and read back the final source/CI evidence. Keep unrun platform/hardware gates explicitly NOT_RUN. Do not merge/delete based on filename counts, author PASS, a wrapper, or a side-branch demonstration. Historical evidence remains intact.
