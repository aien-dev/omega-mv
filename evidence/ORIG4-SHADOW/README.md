# ORIG4-SHADOW: default-off shadow advice, host receipts (omega-mv#731)

`run/`: one run of `sh tests/orig4/run.sh` at source commit `345cbdc0` (branch
`feat/731-orig4-shadow-advice`), host `Linux 7.0.0-1019-nvidia aarch64` (DGX Spark, CPU
only), compiler `seed/out/omc` `734f0a39...6b61`, units pinned by `native/orig4/orig4.pin`
(aien-train `c9ae2b24`, omega-bridge `7e1d0d61`, omega `3a995ba8`, aien-train `c7642d96`).
Per step: `.stdout`, `.stderr`, `.rc`, `.wall_ms`; `summary.txt`; `raw.sha256`, `build.sha256`.
Fixture evidence only (SYNTHETIC `sxgen 7 780`, fnv `626048380840256264`).

| item | result |
|---|---|
| in-process tests (`shadow-test.om`) | PASS, 265 checks: advice = selmain E-mode `slpick` on 20 records, every image/record/allow refusal class, non-finite/ood/single/budget predicates, 50 records never outside `oxcand` |
| advisor over the fixture, MLP candidate (`image_fnv=9232652764948363499`, `bound_matches=no`) | 780 `ADVICE`, 0 `FALLBACK`; adv/oracle 1.0412, incumbent/oracle 1.5797; 91 ms wall for 780 records |
| advisor over the fixture, affine candidate (`image_fnv=3416091170583141300`) | 780 `ADVICE`; adv/oracle 1.0283; 40 ms |
| kill switch (enable 0) | only `SHADOW disabled`, exit 0 |
| process-path refusals | damaged image `image-5`, truncated `image-1` (exit 0, no advice); forged record `records-7`, wrong profile `records-9` (exit 66); REVOKED `allow-12`, unlisted `allow-13` (exit 67); budget 10: 10 `ADVICE` + 770 `FALLBACK budget`; config 68; mode 64; nothing on stderr |
| default-off, static | `OMEGA_EMPIRICAL_RECORDS` readers in src/tests/mk/tools/Makefile: `tests/runtime/rx_empirical_optimizer.c` only; `src/` references to the advisor: none |
| GB10, energy | NOT_RUN, NOT_MEASURED |

## Owner records (local only, development-only; bytes not in the repository)

`ORIG4_RECORDS=~/.local/state/aien-train/orig1/records-20261025T034737Z.bin` (738 records,
fnv `16622604375676129865`, AUTHORIZED), same binaries, output kept outside the tree:

```
SHADOW records=738 data_fnv=16622604375676129865 allow=1
SHADOW image_fnv=9232652764948363499 kind=2 nparams=705 data_bound=16622604375676129865 bound_matches=yes
COST records=738 scored=738 fallback=0 adv_cost=7572063936 oracle_cost=7499958578 incumbent_cost=11877447250   (MLP, 103 ms)
COST records=738 scored=738 fallback=0 adv_cost=7746710230 oracle_cost=7499958578 incumbent_cost=11877447250   (affine, 42 ms)
```

These sums cover every partition including TRAIN (the records the candidates were trained
on): adv/oracle 1.0096 (MLP), 1.0329 (affine), incumbent/oracle 1.5837. They are a lineage
and plumbing receipt, not a held-out result; ORIG-3 (benchmarks#45) owns the sealed
evaluation. Cross-check: the 738 `ADVICE` arms equal the `model` column of selmain's E
mode per record (`cmp` of the (seq, arm) pairs, identical).
