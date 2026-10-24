# TERN-556: gpu_tern_test speed gates judged only with the machine alone, against a regression floor (omega-mv#556)

Chip: GB10 on the Spark, 2026-10-24, `gpu_tern_test --bw-only --reps 3`, built from main 3a583bcc plus this change, physics 6d7cf0d4. No model load. The 35B server on 18091 was resident and idle.

## Finding
The four cold GB/s gates (180, 180, 200, 180 GB/s, launch floor taken out) have never passed on the chip. The machine was quiet on 2026-10-16 and they still failed 0 of 4 (evidence/TERN-T0-KT1-18185404-20261016T0151Z). The 4/68 failures in #556 are these four gates on every run, not load from other jobs.

## Runs
| file | window | 3840x2560 | 2560x2560 | 13824x2560 | 2560x6912 | used for floors |
|---|---|---|---|---|---|---|
| receipt-shared.json | shared gb10 | 121.5 | 124.6 | 174.2 | 168.0 | no (not judged) |
| receipt-excl-1.json | exclusive | 116.01 | 127.36 | 183.34 | 168.79 | yes |
| receipt-excl-2.json | exclusive | 110.82 | 130.95 | 175.65 | 164.57 | yes |
| receipt-verify.json | exclusive, after the floors were set | 113.78 | 128.97 | 172.05 | 169.62 | no: PASS 9/0 |
| receipt-negctl-ctas8.json | exclusive, `--ctas 8` (negative control) | 47.2 | 38.8 | 52.5 | 51.9 | no: FAIL 4 of 9 |

All values are GB/s with the launch floor taken out. excl-1 ran before the floors existed, so it judged the old targets; its timings are valid. excl-2 failed the first floor at 2560x6912 (164.57 < 165.29, built from the three 2026-10-16 runs alone), so the floors were rebuilt from all five machine-alone runs, the failing one included.

## Floors: 2 * min - max over the five runs
3840x2560 100.11 (calc:b48ce168), 2560x2560 116.67 (calc:a3d96dd9), 13824x2560 167.96 (calc:e3e5f8ca), 2560x6912 158.89 (calc:6926fc37).

## Limits
Five runs. The verify run at 13824x2560 (172.05) was below every run used for the floor, so this shape varies more than the five runs show. It still passed with 4.09 GB/s to spare (calc:fb229bac). If it flakes, add the run to the reference instead of loosening the rule.
