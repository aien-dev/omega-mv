# dudect-style Welch t-tests, om/tls, MEASURED host-only (DGX Spark GB10, 2026-10-26)

Driver: om/tls/test/tls/timing.om (sha in SHA256SUMS / v2/SHA256SUMS). Compiler: rebuilt on 2026-10-26
with omc 2c824805 (OMC_PIN) from the PR's units, both binaries are byte-identical to timing.bin and
v2/timing.bin here (MEASURED, see timing-v3/README.md); the earlier "omc 189ada82" note is unverified.
Run as `nice -n 19 taskset -c CPU timing WHAT N` with CPU 0 and 5. CORE MAP (Main, MEASURED from
/proc/cpuinfo 2026-10-26): cpu0 = Cortex-A725 (efficiency, part 0xd87), cpu5 = Cortex-X925 (fast,
part 0xd85). An earlier version of this file had the map reversed from memory; the measured ratio
(cpu0 ~1.8x slower than cpu5) agrees with the corrected map. So: cpu0-* rows = A725, cpu5-* rows = X925.
N = 20000 (x25519) / 50000 (chacha, aesgcm, cteq x2000 compares). Classes: 0 fixed input, 1 random
input, drawn per measurement from getrandom. Python only counted and summed (n, sum x, sum x^2) per
class, full and cropped at the pooled 90th percentile; Wolfram computed means, variances and t.

v1 (first driver: getrandom called only for the random class):
WolframLanguageEvaluator welch[...] ->
cpu0-aesgcm full t -1.17 crop90 -1.84 | cpu0-chacha full 0.46 crop90 -10.38 | cpu0-cteq full -1.05 crop90 0.25
cpu0-x25519 full -0.41 crop90 -1.12 | cpu5-aesgcm full -0.61 crop90 0.32 | cpu5-chacha full 0.82 crop90 5.14
cpu5-cteq full -0.88 crop90 3.61 | cpu5-x25519 full 0.05 crop90 3.35
Reading: the cropped chacha rows exceed dudect's |t| > 4.5 flag (mean difference 3.2 ns of 9.6 us on
cpu0, 0.9 ns of 4.58 us on cpu5). The classes differed in what ran before the timed window (a
getrandom syscall only for class 1), so the driver, not the cipher, was the suspected cause.

v2 (both classes call getrandom; class 0 then overwrites with the fixed pattern; v2/):
WolframLanguageEvaluator welch[...] ->
cpu0-aesgcm full {42972.9, 43582.7, -1.01} crop90 {42842.7, 42841.3, 0.69}
cpu0-chacha full {9661.2, 9655.1, 1.35} crop90 {9624.7, 9623.9, 2.42}
cpu0-cteq full {82783.5, 82780.9, 0.07} crop90 {82369.1, 82379.5, -0.29}
cpu0-x25519 full {319958, 320006, -0.75} crop90 {318710, 318739, -1.12}
cpu5-aesgcm full {21917.1, 21921.1, -1.38} crop90 {21869.1, 21868.6, 0.72}
cpu5-chacha full {4585.9, 4584.9, 0.75} crop90 {4575.1, 4575.4, -1.33}
cpu5-cteq full {37644.9, 37649.0, -1.36} crop90 {37582.1, 37582.4, -1.20}
cpu5-x25519 full {178518, 178362, 1.51} crop90 {178144, 178156, -1.80}
Reading: every |t| < 2.5 (threshold 4.5): no evidence of key-dependent timing in x25519,
chacha20-poly1305, aes-128-gcm or tls_cteq at this sample size on this host. This is MEASURED on one
host with the OS scheduler and the resident model running; it is not a proof and not a hardware
qualification. The v1 chacha flag disappeared when the pre-window state was equalized.

v1 root cause (TlsTiming, 2026-10-26, MEASURED; full data and Wolfram query in
evidence/om-tls-review-2026-10-26/timing-v3/README.md): v1 and v2 differ in one change only
(class 0 also calls getrandom before the window). chacha N=50000, 8 alternating reps per build:
on A725 cores 0 and 2, v1 flags |t_crop90| > 4.5 in 16/16 reps (t -4.55 to -9.25) and v2 in 0/16;
on X925 cores 17 and 5 the v1 effect is weaker and of mixed sign (4/16 flagged), v2 0/16.
Reverting the change reproduces the signal and re-applying it removes it: root-caused to the
asymmetric getrandom in the driver on the A725 cores. The micro-architectural mechanism is an inference.
Superseded as the current screen by v3 (balanced, interleaved, symmetric driver, hkdf added; same
timing-v3/README.md): tls_cteq flagged intermittently on X925 core 17 (|t| up to 27.97, both signs,
5/15 runs), not root-caused; no flag for x25519, chacha, aesgcm, hkdf on cores 17 and 2.
