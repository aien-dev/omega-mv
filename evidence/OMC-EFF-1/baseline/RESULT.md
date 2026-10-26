# OMC-EFF-1 baseline: seeded omc at 3364a1dfd against gcc C twins

Protocol: `bench/omc_efficiency/PROTOCOL.md`, sha256
`ae1d1b2ace22bd30993f2ac14965d0e5ec62d9d66ae4b4e9521a417e5c74033a`, frozen in commit
`951de2ebbeb4c1859b00fd34b230844a5448c6a2` (pushed as `perf/omc-efficiency-baseline` before the run).
Subject: omc sha256 `befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d` (omega-mv
`3364a1dfd0ca50c1b82bf19ba34932d8bc10a7a9`). Oracle: gcc 13.3.0, `-O2 -static`; reference `-O0 -static`.
Counters: perf 7.0.14. Host: DGX Spark GB10, Linux 7.0.0-1019-nvidia aarch64, pinned to CPU 19
(Cortex-X925, 3.9 GHz max), `nice -n 19`. Run 2026-10-26T15:44:03Z, one declared run, no rerun.
Load average 8.69 at start, 12.84 at end (shared machine, not quiesced). Evidence level: MEASURED on the
host. No GPU, no hardware qualification.

## Validity (PROTOCOL section 5): VALID

1. Build mode `S rows=231 ok=231 fail=0`, exit 0: every compile reproduced its reference bytes.
2. Run mode `S rows=1176 ok=1008 fail=0 ctl_detected=168 ctl_missed=0`, exit 0: every real run printed
   exactly the Wolfram-computed `want/` bytes with status 0, and all 168 mutant runs were rejected.
3. `instructions:u` max/min per (case, arm): 1.000000 everywhere (omc counts are exact; C counts vary
   by at most 6 instructions out of 10^8..10^9).
4. `selftest.log`: 8/8 PASS (wrong expected output, a matching control and a wrong build reference
   are each rejected with exit 1; usage errors exit 2).

## Programs: omc / cO2 (21 paired rounds; median, 95% interval x(6)..x(16), coverage 0.9734)

Wall time, ns (fork to waitpid; includes process start for every arm):

| case | omc median (min..max) | cO2 median (min..max) | cO0 median | omc/cO2 [95%] | cO0/cO2 |
|---|---|---|---|---|---|
| arith | 120,363,347 (120,128,019..131,565,960) | 35,852,097 (35,555,230..86,354,432) | 94,693,439 | 3.360 [3.330, 3.378] | 2.641 |
| branch | 146,681,886 (146,221,258..156,999,736) | 73,802,818 (73,447,792..80,432,100) | 323,109,160 | 1.990 [1.982, 1.995] | 4.380 |
| fib | 136,641,182 (136,264,684..144,773,307) | 34,252,340 (33,986,147..40,070,096) | 127,913,836 | 3.994 [3.977, 4.006] | 3.744 |
| leafcall | 53,621,560 (53,315,126..57,753,015) | 15,994,361 (15,839,353..17,105,794) | 74,065,731 | 3.356 [3.337, 3.375] | 4.619 |
| sieve | 247,316,647 (245,635,791..270,673,536) | 117,503,563 (113,403,103..167,831,344) | 812,642,759 | 2.106 [2.091, 2.124] | 6.942 |
| isort | 78,189,234 (77,843,169..90,237,389) | 39,867,615 (39,209,643..61,337,954) | 206,951,515 | 1.969 [1.642, 1.984] | 5.181 |
| matmul | 18,708,718 (17,968,137..23,529,955) | 11,049,684 (10,777,170..15,572,646) | 54,098,363 | 1.649 [1.631, 1.679] | 4.867 |
| geometric mean | | | | 2.501 | 4.458 |

`cycles:u`:

| case | omc median (min..max) | cO2 median (min..max) | cO0 median | omc/cO2 [95%] | cO0/cO2 |
|---|---|---|---|---|---|
| arith | 466,359,514 (466,352,624..466,366,074) | 137,155,005 (136,296,806..137,825,193) | 366,588,597 | 3.400 [3.397, 3.404] | 2.669 |
| branch | 568,927,996 (568,018,955..571,182,142) | 284,832,276 (283,429,655..285,060,372) | 1,254,539,815 | 1.998 [1.994, 2.001] | 4.404 |
| fib | 529,388,142 (528,460,166..529,601,480) | 131,049,850 (130,844,037..131,539,402) | 495,489,931 | 4.039 [4.035, 4.041] | 3.782 |
| leafcall | 206,421,156 (206,280,480..206,676,054) | 60,159,623 (60,104,451..60,628,561) | 285,165,020 | 3.431 [3.427, 3.433] | 4.741 |
| sieve | 955,533,448 (950,618,663..957,973,873) | 447,702,787 (439,154,071..453,280,552) | 3,155,639,065 | 2.133 [2.119, 2.139] | 6.993 |
| isort | 301,626,421 (301,569,474..301,814,221) | 151,062,954 (150,952,156..193,948,563) | 797,423,364 | 1.997 [1.944, 1.998] | 5.261 |
| matmul | 67,395,948 (67,169,838..68,539,499) | 39,089,179 (38,825,474..39,942,959) | 205,095,951 | 1.723 [1.720, 1.729] | 5.253 |
| geometric mean | | | | 2.545 | 4.556 |

`instructions:u` (deterministic):

| case | omc | cO2 | cO0 | omc/cO2 | cO0/cO2 |
|---|---|---|---|---|---|
| arith | 680,000,770 | 560,095,659 | 1,520,095,568 | 1.214 | 2.714 |
| branch | 2,029,124,699 | 929,136,374 | 1,951,393,244 | 2.184 | 2.100 |
| fib | 2,340,101,671 | 994,852,230 | 1,834,228,315 | 2.352 | 1.844 |
| leafcall | 870,000,917 | 360,095,751 | 930,095,667 | 2.416 | 2.583 |
| sieve | 7,139,388,469 | 1,376,344,641 | 6,084,501,505 | 5.187 | 4.421 |
| isort | 2,707,063,875 | 802,211,060 | 2,506,926,366 | 3.375 | 3.125 |
| matmul | 600,130,654 | 164,893,584 | 630,920,678 | 3.640 | 3.826 |
| geometric mean | | | | 2.667 | 2.825 |

All statistics: WolframLanguageEvaluator over the raw rows in `wl-data.m` (`wolfram-wall.txt`,
`wolfram-cyc.txt`, `wolfram-ins.txt`). Plainly: omc's programs take 2.5x the time of gcc -O2 across the
suite (1.65x to 3.99x per case). Against gcc -O0 the median paired wall ratio omc/cO0 per case is
arith 1.273, branch 0.454, fib 1.067, leafcall 0.725, sieve 0.305, isort 0.378, matmul 0.342; geometric
mean 0.562, i.e. omc is 1.78x faster than -O0 overall but slower than -O0 on arith and fib
(`wolfram-prose-checks.txt`).

## Compile time (10 rounds) and code size

| case | omc median ns | cO2 median ns | cO0 median ns | omc/cO2 |
|---|---|---|---|---|
| arith | 511,644 | 35,498,735.5 | 32,269,446 | 0.0146 |
| branch | 489,620 | 36,202,788 | 32,404,015.5 | 0.0135 |
| fib | 489,980 | 45,761,781.5 | 31,654,754 | 0.0105 |
| leafcall | 504,316 | 36,416,254.5 | 32,256,278.5 | 0.0138 |
| sieve | 532,468 | 39,186,435.5 | 32,505,408 | 0.0134 |
| isort | 532,324 | 39,581,006 | 33,418,695 | 0.0132 |
| matmul | 517,820 | 41,386,172 | 33,052,693 | 0.0121 |

Geometric mean omc/cO2 compile time 0.0130 (gcc time includes cc1, as and the static link; both
include fork and exec). Code size: omc ELF bytes (whole program: 176-byte header, putnum, trap stubs,
entry) against all `.text*` bytes of `gcc -c` of the twin:

| case | omc file | cO2 `.text*` | cO0 `.text*` | omc/cO2 | omc/cO0 |
|---|---|---|---|---|---|
| arith | 728 | 164 | 244 | 4.44 | 2.98 |
| branch | 792 | 148 | 264 | 5.35 | 3.00 |
| fib | 780 | 1064 | 156 | 0.73 | 5.00 |
| leafcall | 776 | 132 | 220 | 5.88 | 3.53 |
| sieve | 1080 | 288 | 452 | 3.75 | 2.39 |
| isort | 1372 | 360 | 628 | 3.81 | 2.18 |
| matmul | 1244 | 456 | 792 | 2.73 | 1.57 |

Geometric means 3.27 (omc/cO2) and 2.78 (omc/cO0). Deviation, stated: the protocol named the `.text`
section; at -O2 gcc places `main` in `.text.startup`, so the declared column (`sizes.txt`) reads 0 for
arith, branch, isort and matmul. The table above sums every `.text*` section of the same objects
(`size -A`), a correction made after the run; both columns are kept. The omc figure also counts the
ELF header and runtime stubs, which the C objects do not; it is not a like-for-like kernel size.

## Where omc's instructions go (PROTOCOL section 7, MODELED, reconciled)

Every instruction of every omc hot path was classified (`attribution-labels.tsv`) and multiplied by
trip counts computed with Wolfram (`attribution-blocks.tsv`, `wolfram-attribution-queries.txt`,
results `wolfram-attribution.txt`). Every case reconciles with the MEASURED instruction total to
better than 1.1e-6 (the residual is the putnum calls and the 5-word entry, left out of the model).
Classes: CONST = `mov`/`movk` of an immediate (loop limits, array lengths for bounds checks, global
base addresses, the while-guard limit 1000000, operands); BOUNDS = the run-time index check
(`cmp`, `b.hs` trap 3); GUARD = the while pass counter (`cmp`, `b.hs` trap 4, `add`); CALLSAVE =
stack check, `str`/`ldr` of registers around calls, `sp` adjust; CALL = `bl`/`ret`; CTRL = loop and
condition compares and branches; WORK = the rest.

| class | instructions (suite) | share of omc instructions | share of excess over cO2 |
|---|---|---|---|
| CONST | 5,039,477,599 | 30.8% | 45.1% |
| CTRL | 3,351,060,368 | 20.5% | 30.0% |
| WORK | 3,062,412,058 | 18.7% | 27.4% |
| CALLSAVE | 1,907,904,388 | 11.7% | 17.1% |
| GUARD | 1,347,302,271 | 8.2% | 12.1% |
| BOUNDS | 1,344,666,854 | 8.2% | 12.0% |
| CALL | 312,984,072 | 1.9% | 2.8% |

(omc total 16,365,811,055; cO2 total 5,187,629,299; excess 11,178,181,756; shares of excess add to more
than 100% because cO2 also executes control and work instructions.)

Per case (CONST as a share of that case's excess; IPC = instructions per cycle, from the medians):

| case | excess instructions | CONST / excess | excess cycles | omc IPC | cO2 IPC | largest omc class |
|---|---|---|---|---|---|---|
| arith | 119,905,111 | 1.668 | 329,204,509 | 1.46 | 4.08 | WORK 52.9% (udiv + msub for `rem t 1000003`) |
| branch | 1,099,988,325 | 0.402 | 284,095,720 | 3.57 | 3.26 | CTRL 37.1%, CONST 21.8%, GUARD 19.4% |
| fib | 1,345,249,441 | 0.000 | 398,338,292 | 4.42 | 7.59 | CALLSAVE 64.9% |
| leafcall | 509,905,166 | 0.235 | 146,261,533 | 4.21 | 5.99 | CALLSAVE 44.8% |
| sieve | 5,763,043,828 | 0.565 | 507,830,661 | 7.47 | 3.07 | CONST 45.6% |
| isort | 1,904,852,815 | 0.421 | 150,563,467 | 8.97 | 5.31 | CONST 29.6% |
| matmul | 435,237,070 | 0.501 | 28,306,769 | 8.90 | 4.22 | CONST 36.4% |

## Hotspot facts for the compiler owner

1. **Loop-invariant immediates are re-materialized on every pass** (MEASURED count, MODELED split).
   omc emits `mov`/`movk` for the same constant at each use inside the loop: the loop limit at every
   head (`for` with a number count above 4095, every `while` guard limit 1000000), the array length
   before each bounds check, the global base address before each load or store, and operand constants
   (`mul h 31`, `rem t 1000003`, `mix i 2654435761`). Example, matmul inner loop (`disasm/matmul.omc.s`
   0x2e0..0x334): 22 instructions per pass, 8 of them `mov`/`movk` of 90000 (twice), the base of `ma`
   and the base of `mb`. gcc -O2: 6 per pass (`ldr`, `add`, `ldr` post-index, `madd`, `cmp`, `b.ne`). This class is 45.1% of the suite's excess
   instructions, the largest single class. Caveat: X925 issues wide; where omc's IPC is 7.5..9
   (sieve, isort, matmul) these extra instructions cost few cycles, so the cycle gain from removing
   them is smaller than their instruction share. [INFERENCE until measured]
2. **Call save/restore stores every in-use register, live or dead, and reloads through the stack**
   (MEASURED count). fib (`disasm/fib.omc.s` 0x204..0x274): 24 save/restore/stack-check instructions
   per non-leaf invocation of fib, which makes two calls (WolframLanguageEvaluator:
   `(1517903654-14)/(Fibonacci[39]-1)` -> `24`; 14 are main's), including stores of x1 before it is written, of the argument temporary x2/x3,
   and a reload of x0 after the last use of `n`; the argument is reloaded from its save slot instead
   of moved. CALLSAVE is 64.9% of fib's and 44.8% of leafcall's instructions; fib has the largest
   cycle ratio in the suite (4.04x) and the largest wall ratio (3.99x). In leafcall the 2-instruction
   constant argument `2654435761` is also re-materialized each call.
3. **Division and remainder by a constant use `udiv`** (MEASURED cycles, mechanism [INFERENCE]).
   arith: omc executes only 1.21x gcc's instructions but 3.40x its cycles; omc IPC 1.46 against 4.08.
   gcc replaces `t % 1000003` with `umulh`/shift/`msub` (`disasm/arith.cO2.s`); omc emits `udiv` +
   `msub` (`disasm/arith.omc.s` 0x228..0x234). The same pattern sits in matmul's fill (`rem .. 13`,
   `rem .. 11`), isort's generator (`rem .. 2147483648`, a power of two, and `rem .. 100000`) and in
   every putnum (`rem x 10`, `div x 10`).
4. **Loop shape**: every loop tests at the top and jumps back unconditionally (CTRL, 20.5% of omc
   instructions); gcc tests at the bottom. The `while` guard (GUARD, 8.2%) and bounds checks (BOUNDS,
   8.2%) are mandatory Omega semantics, not removable without a proof; their limit constants are
   counted under CONST.
5. **Register pressure refusal**: the direct matmul inner expression
   `add s mul get ma add mul i 300 k get mb add mul k 300 j` is refused with error 12 (more than 16
   registers); the kernel names the row and column walk instead (PROTOCOL section 2).

Ranking by excess cycles (MEASURED): sieve 507.8M, fib 398.3M, arith 329.2M, branch 284.1M, isort
150.6M, leafcall 146.3M, matmul 28.3M (total 1,844,600,951). Which compiler change converts the most
of that into time is not measured here: it needs the section 8 A/B on a candidate compiler.

## What was not run

- No candidate compiler and no A/B (PROTOCOL section 8 is declared, not exercised).
- No `perf record` or other per-instruction cycle attribution; the cycle mechanisms in facts 1..3 are
  [INFERENCE] from instruction counts, IPC and disassembly.
- No GPU, no hardware qualification, no quiesced machine.

## Files

`run.raw`, `build.raw` (conductor rows), `provenance.txt`, `inputs.sha256` (protocol, conductor,
run.sh, kernels, want), `arms.sha256` (every binary run), `sizes.txt` (declared size column),
`selftest.log`, `wl-data.m` (the rows as Wolfram data), `wolfram-*.txt` (every statistic),
`attribution-*.tsv`, `wolfram-attribution*.txt`, `disasm/` (omc and gcc -O2 disassembly).

Replay: `git checkout 951de2eb && sh bench/omc_efficiency/run.sh seed/out/omc OUTDIR 19`, then the
queries in `wolfram-*.txt` on the new rows.
