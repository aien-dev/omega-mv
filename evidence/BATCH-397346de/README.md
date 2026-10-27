# Compiler batch 3 (pin 397346de)

This batch was EXERCISED on the arm64 Linux host (DGX Spark, cores 15-19, nice -n 19). That is not GPU qualification.

- Integration branch: `build/omc-batch3-896-897-898-903`, cut from main `e5b32567`. It merges, in this order:
  - #896, integer bitcast re-extension (canonicalize the narrowed integer after `bitcast`)
  - #897, an empty text is sized `nm_len 0` and a view over it counts 0 (alternative to #835)
  - #898, function names that start with a keyword
  - #903, `pread`/`pwrite` check that the destination is writable
- Compiler: `seed/out/omc` sha256 `397346de169d78e8a5ce53dce6cf9f0ca56a8f527128165bbfb4fb62a335f93e`, from the om4 → stage1 → stage2 fixpoint (`rebirth.log`: `PASS omc fixpoint 397346de…`).
  - Source `seed/omc/omc.om`: sha256 `0b532257debbdec357fbec75bdb1433fd2154d1b5bec3c4f50a35424793af9e6`, 12,072 lines, 407 functions.
  - Size: 326,508 bytes. `WolframLanguageEvaluator: (326508-176)/4 -> 81583` words.
- `om/units.sh` pins this compiler; bindings regenerated from omega-bridge `431fff0a`; `size-history.tsv` gained the row `RM-batch3`.
- Gate scripts whose compiler pin is hard-coded were moved to this compiler: `seed/json/tests/gate-json.sh` (was 2c824805), `seed/wasm/gate-irwasm.sh` and `seed/spirv/gate-irspirv.sh` (both still named befb8199, so on main they already failed `compiler pin differs`).
- `seed/omc/om2/s3/mut/e16.from`/`.to`: the anchor predated the `cp_head`/`cp_skip` lines that contracts (`b2869978`) added to `s_unroll`, so on main the substitution matched 0 times and `m-e16-d3`/`m-e16-inl` were NOT_RUN (suite INVALID). Both files now carry the two lines; the mutation itself (`fuelc … kk` → `1`, plus the per-iteration charge in `s_unr`) is unchanged.

## Per-PR regressions (old pin 2c824805 vs this compiler)

The rows each PR added, run through `seed/qualification/om2/gate.om`. Case files and logs are in `regressions/`.

| PR | Rows | 2c824805 | 397346de |
|---|---|---|---|
| #896 | `bitcast-int` run, `bitcast-width` refuse 7 | rc 1 (FAIL) | rc 0 PASS |
| #897 | s1/s4 empty-text rows | rc 3 (one case killed by signal 139; the rest FAIL: exit 1/231/7/8 instead of 0/103, and `v-bad-text-empty` compiled instead of refusing 21) | rc 0 PASS |
| #898 | keyword-prefix rows | rc 1 (FAIL) | rc 0 PASS |
| #903 | pread/pwrite writable rows | rc 1 (FAIL) | rc 0 PASS |

## Checks

| Check | Result | Log |
|---|---|---|
| `tests/gates/run.sh seed` (`seed/host/check.sh`: C oracles, gate-ir, gates 7..18 incl. gate8 `bitcast_int`/`neg_bitcast_width`, contracts and mutants, native host fixtures) | rc 0, `PASS gate8 29 cases`, `PASS native-host qualification` | seed.log |
| `seed/omc/gate5.sh` referee | rc 0, GATE5 PASS (size history last row RM-batch3) | gate5.log |
| OM-2 s1 / s2 / s3 / s4 | PASS ×4 | s1.log … s4.log |
| OM-2 s1 / s2 / s3 / s4 mutants | rc 1 each, the expected outcome: every `m-*` FAIL (killed), every control PASS; 18 / 14 / 36 / 17 verdicts | s*-mutants.log |
| Frozen `manifest-c2.txt` | 5870/5876; the 6 failures are exactly the declared c2-00015/00033/00728 (batch 2) and c2-00304/00306/00675 (capacity). No new delta. | manifest-c2.log |
| `manifest-c2-capacity.txt`; `manifest-c2-batch2.txt` | 3/3; 3/3 | manifest-c2-*.log |
| `tests/gates/run.sh om-units` | rc 0, SHA256-PARITY PASS, BLAKE3-PARITY PASS | om-units.log |
| `seed/json/tests/gate-json.sh` | rc 0, JSONGATE PASS rows=8758 mutants=5 | json.log |
| `seed/net/tests/check.py` | rc 0, HTTP-CHECK PASS | net.log |
| `om/tls/test/*/run.sh`, eight suites | all rc 0 PASS; `tls` net=NOT_RUN (TLS_NET unset, no network traffic in this run) | tls*.log |
| `seed/wasm/gate-irwasm.sh` | rc 0, PASS IRWASM native parity cases=129 | irwasm.log |
| `seed/spirv/gate-irspirv.sh` (SPIRV-Tools v2025.1 spirv-val and PoCL 2.16.0 CPU from `~/work/E1Open/tools`, the E1Open recipe `pocl-env.sh`) | rc 0, PASS IRSPIRV CPU OpenCL parity cases=129 deterministic=16 validated=17 refusals=16; old-pin control (2c824805) rc 1 `FAIL compiler pin differs` | irspirv.log, irspirv-oldpin.log |
| `gate-ir-trap.py`; `mutants-ir-trap.py` | RC=0; 5/5 caught | ir-trap*.log |

Pin negative controls (working clone, not logged here): with the 2c824805 compiler the JSON gate ends
`JSONGATE FAIL … differs from pin 397346de…` (rc 1) and the x509 suite ends `FAIL omc is not the pinned compiler` (rc 1).

## Not run

- TLS network parity (`TLS_NET=1`): NOT_RUN.
- `seed/challenge/run.sh` stays pinned to its published source commit and compiler 2c824805 by design; it was not moved.
- No GPU, silicon or chip gate was run.
