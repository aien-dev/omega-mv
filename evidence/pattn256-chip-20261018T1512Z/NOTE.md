# First chip run with the encoder fix (omega-mv#545)

Attention parity passed at every point (worst 0.007 of the tolerance); no fault at any size up to 129024+2048.
The 2 FAILs are the row check's ruler, not the kernel: one K value per run (30720+2048 token 961: chip 36f0 ref 36f3;
129024+2048 token 871: chip b637 ref b63a) is a value of ~1e-5 left by the rotation's cancellation, 3 bf16 ulps from
the double oracle; the chip rows are bit-equal to the decode program's rows at the same points (twin: 0 words differ).
The check was widened to "1 ulp, or within 8 f32 roundings of the row's largest |k|" (tests/gpu_prefill_attn256_test.c
rows_ok); the rerun is evidence/pattn256-chip-20261018T1517Z.
The sweep beside it (evidence/encoder-fix-sweep-20261018T1512Z): 18/19 families PASS on the chip; gpu_tern_test FAILs only its
4 GB/s speed gates, and the unfixed encoder fails the same 4 gates in an A/B on the same load (new 119.3/116.7,
old 112.3/112.6 GB/s at 3840x2560), so the gates measured host load, not the fix.
