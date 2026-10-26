# Compiler batch: capacity (#768) + f32 return (#812) + contracts (#807)

EXERCISED on the arm64 Linux host; not GPU qualification.

- Source: `feat/omc-capacity` at the commit carrying this receipt. Its parent `eb81528821093b07f69973d01a25c3466230acac` merges #812 into the integration branch, which already includes current main `3a10bd29` and #807.
- Compiler: `seed/out/omc` SHA256 `189ada82e00bb440cb41351bd252cc68c54a1b1050051cc42a1df700081c3b54`. It reached the om4 → stage1 → stage2 fixpoint. The source is 11,555 lines with 390 functions and compiles to 310,224 bytes. Word count: `WolframLanguageEvaluator: (310224-176)/4 -> 77512`.
- `om/units.sh` pin and the generated bindings were regenerated from omega-bridge `431fff0a` with this compiler, and `size-history.tsv` gained the row `RM2-capacity-f32`.

## Checks (all under nice -n 19 ionice -c3, -j2)

| Check | Result | Log |
|---|---|---|
| C oracles built, `seed/host/check.sh` (gate-ir with oracles, gates 7..18, contracts 44/44, contract mutants, C parity, native host) | RC=0 | qualification.log |
| `seed/omc/gate5.sh` referee (fixpoint, om4 corpus, C path, mutants, nextfuzz, errors 1..23, size ledger last row) | GATE5 PASS | qualification.log |
| gate8 floats, including the new `f32_return` plus `neg_void_value` (error 7) and `neg_f32_fall` (error 18) | 26 cases PASS | qualification.log |
| gate12 / gate15 capacity boundaries: 8192 names accepted, 8193 refused; 4095 functions accepted, 4096 refused (error 14) | PASS | qualification.log |
| `gate-ir-trap.py`; `mutants-ir-trap.py` | RC=0; 5/5 mutants caught | ir-trap.log |
| Frozen C2 `manifest-c2.txt` | 5873/5876. The three failures are exactly the declared capacity delta below | qualification.log |
| Declared delta `manifest-c2-capacity.txt` | 3/3 PASS | c2-capacity.log |
| Negative control: the pre-capacity main compiler befb8199 on the delta | 0/3, exit 14 (refused) | c2-capacity-negative.log |

The f32 negative control is in #812: the pre-fix compiler refuses `f32_return.om` at line 4 with error 5.

## Declared C2 delta

`manifest-c2.txt` is frozen and unchanged. Rows c2-00304 and c2-00675 (1025 names) and c2-00306 (513 functions) expected error 14 at the old capacity. They now compile, as #768 intends. Each of the three new binaries ran with exit 0. `seed/qualification/om2/manifest-c2-capacity.txt` records their new output hashes. Historical evidence under `evidence/SEED-0` and `evidence/CONTRACTS` is unchanged (`git diff --exit-code eb815288`).

## Not covered

`fp_conv` unsigned and narrow float-to-integer saturation is not addressed here and stays a separate queue item. The same goes for the open source PRs #805 flock, #806 function-mode const and #808 network host words. #789 remains BLOCKED_OPERATOR.
