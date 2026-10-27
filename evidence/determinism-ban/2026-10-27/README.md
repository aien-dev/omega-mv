# Determinism ban gate, host run 2026-10-27

Command: `sh seed/detcheck/gate-det.sh` at det/ban-order-dependent `b5869f00e2a8156f82da17e6b362115d08b4d689`
(clean tree, `dirty=0` in `gate.log`), on the DGX Spark host (Linux 7.0.0-1019-nvidia aarch64). Exit 0, verdict PASS.

- Compiler: `seed/out/omc` sha256 `2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f` (matches `seed/out/omc.sha256`).
- detcheck binary sha256 `b03f301c5bc0081405dcc2be97197cd37d3c7e340a4abe0a3fb79909987fa2d8`.
- Referee checker2 sha256 `600fcadaac7ad8771fb22dcb0d4803ff3e415a2f3012fe0bd42d84e62b5274f8`, built by `make build/checker2/checker2` from the same commit.

Files: `gate.log` (every line the gate printed: 13 kernels accepted by both checkers with their IR sha256,
15 race fixtures refused by checker2 with their named rule, 24 ATOM cases), `cases.want` / `cases.out`
(expected and observed `selector exit word`), `mutant_*.diff` (the wrong verdicts each rule-dropping mutant produced).

Scope: HOST, checker behaviour only (EXERCISED). No GPU was used. No producer or back end emits ATOM, so no
atomic ran anywhere; same-bits on the chip is D7 (aien-dev/CUDApp#141).
