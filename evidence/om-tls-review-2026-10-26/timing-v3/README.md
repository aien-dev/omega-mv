# om/tls timing screen v3 and the v1 root cause (DGX Spark GB10, 2026-10-26)

This is a screen on one busy host, not a proof of constant time and not a hardware qualification.
All numbers below are MEASURED on 2026-10-26 between 21:20Z and 21:56Z; every t and df was computed
by WolframLanguageEvaluator from the per-class sums in the `sums.json` files (Python only counted).

## Setup

- Core map, MEASURED from `/proc/cpuinfo`: cpus 0-4 and 10-14 are part 0xd87 (Cortex-A725), cpus 5-9
  and 15-19 are part 0xd85 (Cortex-X925). Core 17 = X925, core 2 = A725 (also 5, 9 = X925; 0 = A725).
  `cpuinfo_avg_freq` read 3876996-3900000 kHz on cores 17 and 9 during `cteq2/` (`cteq2/load.log`).
- Compiler: `seed/out/omc`, sha256 `2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f`
  (= `OMC_PIN` in `om/units.sh`). Build: `sh om/tls/build.sh seed/out/omc DRIVER.om OUT`; every
  build rc 0 with empty stderr.
- Drivers (sha256 in `SHA256SUMS`):
  - v3 = `om/tls/test/tls/timing.om` (sha256 `63b5b2f3…3bce`, binary `2cd6c54c…2023`). Classes are
    balanced and randomly interleaved (selection sampling from getrandom). Both classes call
    getrandom for the input, then every byte is rewritten through the same mask expression (fixed
    pattern for class 0, random for class 1), so the code before the timed window is the same for
    both classes. Operations: `x25519`, `chacha` (seal), `aesgcm` (seal), `cteq` (2000 `tls_cteq`
    compares of 32 bytes; exits 3 unless class 0 compares equal and class 1 does not), `hkdf`
    (HKDF-SHA256 extract with a 32-byte class secret, expand to 32 bytes).
  - `v1r.om`: the first driver (class 0 skipped getrandom), reconstructed; sha256 `d9e4af24…375f`
    equals the original v1 source sum. Built with 2c824805 it gives `f3eb8caa…9a0e`, byte-identical
    to the original v1 `timing.bin`. `v2.om` gives `6118a157…bfb6`, byte-identical to the original
    v2 `timing.bin` (`../timing/`). v1 and v2 differ only in `tm_fill`.
  - `aa.om`: A/A control = v3 with both classes given the fixed pattern and the cteq class check
    removed. Measurement control only.
- Run: `nice -n 19 ionice -c3 taskset -c CPU ./BIN WHAT N`; each output line is `CLASS NANOSECONDS`.
  `rc/` used `nice -n 19 taskset -c CPU` without ionice.
- Statistics: `sums.py` writes n, sum x, sum x^2 per class for the full data, the crop at the pooled
  90th percentile (`p90`, the v1/v2 statistic) and the dudect crops `dI` (I = 0, 9, ..., 99;
  percentile 1 - 0.5^(10(I+1)/100)). Welch t, Welch-Satterthwaite df and class means in Wolfram:

```
welch[{n0_,s0_,q0_},{n1_,s1_,q1_}]:=Module[{m0=s0/n0,m1=s1/n1,v0=(q0-s0^2/n0)/(n0-1),v1=(q1-s1^2/n1)/(n1-1)},
 N[{(m0-m1)/Sqrt[v0/n0+v1/n1],(v0/n0+v1/n1)^2/((v0/n0)^2/(n0-1)+(v1/n1)^2/(n1-1)),m0,m1}]]
```

  Self-check: `welch[{3,6,14},{3,9,29}]` -> t -1.2247, df 4. The full query (with the data) is in
  `*/wolfram-query.wl` (`rc/wolfram-out.txt` carries its own query line); output is in `*/wolfram-out.txt`.
  t > 0 means class 0 (fixed) is slower. Flags: |t| > 4.5 leak suspect, > 10 definite (dudect).
  The max over 13 tests per run is reported, which is more permissive than a single test.
- Host load (1-minute loadavg from `uptime` before every run, `*/load.log`; 20 cores, a resident
  model and other agents running): rc 21.01-25.00, v3 18.39-27.04, cteq 16.32-27.87, cteq2 21.04-22.08.
- Raw samples: 129 files, sha256 in `RAW-SHA256SUMS`; kept locally (13.7 MB xz) at
  `/home/drakestapleton/handoffs/TlsTiming/raw/timing-v3-raw.tar.xz`, not in the repository.

## 1. v1 chacha t = -10.4: root-caused to the driver

Single change, chacha N = 50000 (about 25000 per class), 8 reps per build alternating v1r/v2,
cores 0, 2 (A725) and 5, 17 (X925). `rc/wolfram-out.txt` columns: run, t_full, t_p90, df_p90,
mean difference (class 0 - class 1, ns, p90 crop).

| core | v1r reps with \|t_p90\| > 4.5 | v1r t_p90 | v2 reps flagged | v2 max \|t_p90\| |
|---|---|---|---|---|
| 0 (A725) | 8/8 | -4.55 to -9.25 | 0/8 | 2.63 |
| 2 (A725) | 8/8 | -5.44 to -8.87 | 0/8 | 2.82 |
| 17 (X925) | 3/8 | +0.84 to +9.26 | 0/8 | 2.44 |
| 5 (X925) | 1/8 | -0.34 to +13.07 | 0/8 | 2.56 |

All 64 t_full are within ±2.25; df_p90 44999-47915. Reverting the one change (v1r) reproduces the
cropped-tail signal on both A725 cores in every rep; re-applying it (v2) removes it on all four
cores. Verdict: ROOT-CAUSED on A725, the flag was caused by the getrandom syscall that only class 1
made before the timed window, not by the cipher. On X925 the v1 effect is weaker, of the opposite
sign and present in only 4/16 reps. Why a syscall shifts the tail of the next window by 1.3-2.9 ns
is an inference (cache, TLB or predictor state disturbed by the kernel entry) and was not measured.
v2 cropped t on A725 is positive in 15/16 reps (0 to 2.82): a small residual, below the flag.
v3 removes the remaining asymmetry (both classes run the same instructions before the window).

## 2. v3 screen, cores 17 (X925) and 2 (A725), 3 reps each (`v3/`)

N per run (n per class exactly N/2): x25519 100000, chacha 500000, aesgcm 200000, cteq 100000,
hkdf 500000. 30/30 runs rc 0. Max |t| over the 13 tests of each run:

| primitive | core 17 (X925) r1 / r2 / r3 | core 2 (A725) r1 / r2 / r3 | flag |
|---|---|---|---|
| x25519 | 2.33 / 1.73 / 1.54 | 1.12 / 1.80 / 2.81 | none |
| chacha20-poly1305 seal | 1.16 / 0.92 / 1.59 | 2.01 / 1.94 / 1.35 | none |
| aes-128-gcm seal | 2.30 / 1.39 / 0.96 | 2.04 / 1.12 / 1.65 | none |
| hkdf-sha256 | 2.17 / 3.52 / 2.43 | 2.13 / 1.81 / 1.44 | none |
| tls_cteq | **22.51** (p90) / **4.87** (d39) / 1.98 | 1.60 / 1.73 / 1.29 | X925 suspect |

Full-data |t| for every v3 run is at most 1.67 (core 2 chacha r1).

### tls_cteq on X925: leak suspect, not root-caused

Follow-up, cteq N = 100000, v3 against the A/A control, alternating, 6 reps per core (`cteq/`), then
6 more v3 reps on cores 17 and 9 (`cteq2/`):

| set | core 17 (X925) max \|t\| per rep | other core |
|---|---|---|
| v3 (`cteq/`) | **27.97** (d29), **6.41** (p90), **-25.88** (p90), 2.00, 2.92, 2.57 | core 2: 2.49, 2.08, 2.09, 2.67, rep 6 NOT_RUN (killed by job deadline) |
| A/A (`cteq/`) | **5.27** (d29), 2.34, 2.55, 2.40, 1.36, 3.33 | core 2: 2.43, 2.59, 1.72, 1.82, 2.29, 1.52 |
| v3 (`cteq2/`) | 3.30, 1.51, 3.15, 1.98, 1.17, 3.17 | core 9 (X925): 2.04, 2.09, 2.58, 1.35, 1.97, 1.44 |

- On core 17, 5 of 15 v3 cteq runs flagged (three above 20, of both signs); the A/A control flagged
  1 of 6 at 5.27. All five flagged v3 runs started between 21:26:57Z and 21:37:15Z; none of the 15
  later v3 runs on X925 (core 17 reps 4-6 in `cteq/`, cores 17 and 9 in `cteq2/`) flagged.
  Full-data |t| stayed at most 2.58 in every run.
- In the flagged runs the cteq times are bimodal: about 36.5 us and a mode about 5 % slower at about
  38.5 us. The class difference sits in the slower mode only, and its direction changes between runs
  (class 1 slower in `cteq/cpu17-v3-r1`, class 0 slower in `cteq/cpu17-v3-r3`); the crop threshold
  falls inside that mode. (Exploratory per-tenth percentiles, counted in Python, not a reported statistic.)
- The machine code of `tls_cteq` in v3.bin (`tls_cteq.dis`, objdump of the static binary) branches
  only on the loop index and bounds; the secret bytes reach `ldrb`, `eor`, `orr`, `add`, `lsr` and
  no branch or address. So no secret-dependent branch or memory address was found.
- Verdict: LEAK SUSPECT on X925, not root-caused. The signal is intermittent, of inconsistent sign,
  and confined to a slower timing mode, which fits a host effect (frequency or contention state) more
  than a fixed data-dependent latency, but that is an inference: it was not reproduced in isolation,
  so a data-dependent power/frequency effect (Hertzbleed class), which DIT does not cover, is not
  ruled out.

## Verdict

- x25519, chacha20-poly1305 seal, aes-128-gcm seal, hkdf-sha256: no flag on X925 core 17 or A725
  core 2: max |t| 3.52 over their 24 runs (13 tests each). The screen passed at these N; not a proof.
- tls_cteq: no flag on A725 (core 2) or on X925 core 9; leak suspect on X925 core 17, intermittent,
  not root-caused (above).
- v1 chacha flag: root-caused to the driver's asymmetric getrandom (section 1).

## NOT_RUN

- ec (P-256 ECDSA verify), rsa (RSA-PSS/PKCS#1 verify), x509: verification of public data only, no
  secret input, so no constant-time requirement (README table); not screened.
- ML-KEM (`om/tls/mlkem.om`): not composed into `om/tls/build.sh` at the time of this run.
- tls_cteq core 2 v3 rep 6: killed at 85039/100000 lines by the job deadline; deleted.
- No quiet-host run: no exclusive window was taken; the resident model and other agents were running.
