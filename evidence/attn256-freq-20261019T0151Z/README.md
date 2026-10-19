# attn256 FREQUENCY evidence (omega-mv#570), 2026-10-19

Kernel: Qwen3.8-27B decode attention (omega_blackwell_codegen_attn256.c), V-pass rescale branch. Profile:
docs/turing/F2_FREQUENCY_TIMING_PROFILE_V0.md (v1 amendment). Repeat run of the whole script: ../attn256-freq-20261019T0159Z-repeat/.

| file | what |
|---|---|
| chip-check.log | 29/29 PASS: chip counters == host replica in every warp (4 classes, contexts 4096 and 32768), count mutant red (512/1024 and 4096/8192 words), rescale 0 == 1 bit for bit incl. subnormal V |
| null.tsv / null.log | first look, blocked order: rho1000 vs rho0 at 4096 +2.3% (calc:dd00c451); 128k context 69.5 GB/s (calc:a390a07e) = 0.25 of 273 GB/s (calc:baa5c594) |
| train.tsv, model-v0-confounded.txt | v0 design (one run per class): run-to-run drift (408 vs 306 us, same kernel) read as a branch effect; replaced by v1 before held-out existed |
| train1.tsv, model.txt | v1 train, 1280 launches interleaved in one run (seed 111); frozen fit |
| held1.tsv, held1.sha256 | sealed held-out, 1920 launches (seed 222, intervention rho750 + rho1000), sha256 committed in a7df2e22 before scoring |
| score.txt | T(M; B, D) all -195 bits, intervention -193, in-distribution -130, shuffled control -182; bits: 0 of 1280 and 0 of 1920 launches differ |
| modes-held1.txt | median time, default kernel vs always-rescale, per class and position |

Verdict under the pre-registered rule: the frequency term does NOT earn its keep (T < 0 on every block; the shuffled
control is not below the real score, as expected when the odds carry no timing information). Repeat: -59 / -52 / -135 /
-39 bits. Held-out ran about 10% slower than train at 65535 (4255 vs 3854 us, calc:3d375f78): box load, the same for
both models. At 65535 the default kernel streams K+V at 0.231 of the 273 GB/s ceiling (calc:98727d19).
