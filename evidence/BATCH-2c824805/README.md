# Compiler batch 2 (pin 2c824805)

This batch was EXERCISED on the arm64 Linux host. That is not GPU qualification.

- Integration branch: `integ/omc-batch2`, cut from main `c5d98104`. It contains:
  - #810, IR v1 validation (OmegaMemory)
  - #808, `peercred` 220, `pollfds` 221 and `sendmsg` 222, the sockopt allowlist, and getppid moved to syscall 173 (OmegaNet)
  - #806, function-mode `const` (OwnDriver)
  - #805, `flock` 197 (AllenContacts)
  - #815, saturating float-to-integer casts and the const-after-fn ordering fix (OmegaNext)
- Compiler: `seed/out/omc` sha256 `2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f`, from the om4 → stage1 → stage2 fixpoint.
  - Source `seed/omc/omc.om`: sha256 `7703da00055fc0c7a91fcae43071df76a3f277dbe867cb284a3bdff3bedb7e1b`, 12,040 lines, 405 functions.
  - Size: 325,524 bytes. `WolframLanguageEvaluator: (325524-176)/4 -> 81337` words.
- `om/units.sh` pins this compiler. The bindings were regenerated from omega-bridge `431fff0a`, and `size-history.tsv` gained the row `RM-batch2`.

## Checks (nice -n 19 ionice -c3, -j2)

| Check | Result | Log |
|---|---|---|
| C oracles built; `seed/host/check.sh`, which covers gate-ir with the oracles, gates 7..18, contracts and their mutants, C parity, native host fixtures and 53 host negative controls | RC=0 | qualification.log |
| `seed/omc/gate5.sh` referee | GATE5 PASS | qualification.log |
| gate8, including `convert_sat` (25 checks: both ends of every integer width, truncation, NaN → 0, f32 and f64 sources) | 27 cases PASS | qualification.log |
| gate16, including `const_order` | 19 programs PASS | qualification.log |
| gate18: keyword table parity, now 157 packs with flock and the network words | PASS | qualification.log |
| `gate-ir-trap.py`; `mutants-ir-trap.py` | RC=0; 5/5 caught | ir-trap.log |
| Frozen C2 `manifest-c2.txt` | 5870/5876; the 6 failures are exactly the declared rows below | c2.log |
| `manifest-c2-capacity.txt` (#768) | 3/3 | c2.log |
| `manifest-c2-batch2.txt` | 3/3 | c2-batch2.log |
| Negative control: compiler 189ada82 on `manifest-c2-batch2.txt` | 0/3 | c2-batch2-negative.log |

Negative controls for the fixes:
- The 189ada82 compiler runs `convert_sat` with exit 1, because it leaves uchar 300 uncanonicalized instead of 255.
- The 189ada82 compiler refuses `const_order` with error 16.
- The new instruction encodings were checked against GNU as/objdump, and the decimal values computed with Wolfram.

## Declared C2 deltas

`manifest-c2.txt` stays frozen. The new rows are in the additive file `manifest-c2-batch2.txt`, which records the reason for each:
- **c2-00015:** getppid now uses syscall 173. The new binary exits 0, while the 189ada82 build of the same source exits 7.
- **c2-00033:** the sockopt allowlist adds checking code. The program exits 0 under both compilers.
- **c2-00728:** IR `continue` outside a loop is still error 3, now reported on its own line (#810).

## Not in this batch

- NoSystemd host words 198..219: no PR exists yet, so they move to batch 3.
- `peernam` 223, requested by AllenScience: batch 3.
- #775, the length of an empty text passed to a `view`: batch 3.
- The v7 memory producer, irwasm and oracle: OmegaMemory2 is building them on this branch for a later reseed.
- #789 stays BLOCKED_OPERATOR.
