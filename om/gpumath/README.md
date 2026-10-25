# om/gpumath: host math, FP8 packing and message formatting in seeded Omega

`gpumath.om` ports three C files of `libomega_gpu.a` to seeded Omega (aien-dev/omega-mv#726,
aien-dev/aien-architecture-mv#300/#316):

| C | what it does | Omega functions |
|---|---|---|
| `src/omega_gpu_hostmath.c` | libm-free double `log`, `exp`, `pow`, `sincos`; `pow2f` | `hmlog` `hmexp` `hmpow` `hmsincos` `hmpow2f` |
| `src/omega_gpu_fp8_pack.c` | e4m3 decode, kernel bits, NaN test and scan; bf16 scale and x packing; the f64 GEMV reference | `e4m3ref` `e4m3kbits` `e4m3isnan` `f8scannan` `f8packscales` `f8packxf32` `f8packxbf16` `f8gemvblock` |
| `src/omega_gpu_fmt.c` | the bounded `printf`-style formatter the session and tensor APIs use for error stages | `gpufmt` `gpufmtkinds` |

The task description for this port spoke of bf16/e5m2 conversions and a format descriptor table. The
files contain neither. There is no e5m2 code, and `omega_gpu_fmt` is a formatter (`%s %c %d %u %x`, the
`l`/`ll`/`z` modifiers, `%%`). This port follows the files.

The C is unchanged. This change adds the port and its parity evidence. Removing the C objects from
`libomega_gpu.a` goes through the omega-bridge native call ABI (omega-bridge #8) and is not part of it.

## How the port matches the C bit for bit

The contract is the C as the product builds it: the Makefile CFLAGS (`-std=gnu11 -O2`, gcc 13.3.0,
aarch64). The oracle objects built from these sources have the same `.text` bytes as a `make` build in
another worktree (`omega-blake3-abi/build`, same source sha256s). In detail:

- **Same IEEE operations, same order.** Where the C computes in `float` or `double`, the port uses the
  same single Omega float operation (`f32`/`f64`; omc emits one FP instruction per operator and never
  writes FPCR). It does not substitute integer code that "should" agree. Operand order follows the
  compiled object, not the C source, because it decides which NaN payload survives. gcc emits
  `fmul d0, x, ref` in the GEMV reference (`x` first) and `fmul log(b), e` in `pow`, and the port does
  the same.
- **Fused multiply-add.** `gnu11` implies `-ffp-contract=fast`, so gcc fused nine `a*b+c` in hostmath into
  FMADD/FMSUB: the `log` series step `s*t2 + 1/k`, its two final steps, the two `exp` reduction steps and
  the four `sincos` reduction steps. Omega has no fused operation, so `fma64` computes the fused result
  exactly in integer arithmetic. It forms the 106-bit product in a 128-bit frame and aligns the addend
  with a sticky bit. It rounds once to nearest even, with subnormal results rounded directly at
  2^-1074 and overflow going to infinity. It follows ARM's FPProcessNaNs3 order (addend, then the
  operands) and gives the default NaN for a quiet-NaN addend with ∞·0. FMSUB is `fma64` with the
  multiplicand's sign flipped, as the ARM pseudocode does. The `fma` stream checks `fma64` against the
  hardware `fmadd` instruction.
- **Integer bit patterns** where the C works on integers: `pow2f`, `pow2i`, the e4m3 kernel bits and NaN
  test, the scan, and the whole formatter.

## ABI (bindable by omega-bridge native call ABI v1)

Every exported function has at most 6 parameters, each `ulong`/`long` or a fixed-length `view`/`edit` array of
`ulong`/`uchar`, and returns `ulong`, `long` or nothing. A double or float travels as its bit pattern, and
arrays hold little-endian element bytes exactly as the C arrays hold them in memory. ABI v1 arrays have a fixed length, so the variable-length C
functions become pieces that an adapter loops over, the way `om/sha256` and `om/blake3` do:

| C | Omega | piece |
|---|---|---|
| `omega_fp8_pack_scan_nan(w, count, &first)` | `f8scannan w(view uchar 4096) n base st(edit ulong 2)` | 4096 bytes; `st` = count, first (written only while the count is 0) |
| `omega_fp8_pack_scales(bf16, count, out, nofold)` | `f8packscales in(8192 B) n out(16384 B) nofold → long` | 4096 elements; 0 or -1 after writing the earlier outputs |
| `omega_fp8_pack_x_f32` / `_x_bf16` | `f8packxf32` / `f8packxbf16 in n out` | 4096 elements |
| `omega_fp8_gemv_oracle(w, s, n, k, x, y, yscale)` | `f8gemvblock w(128 B) x(512 B) sb acc(edit ulong 2)` | one 128-column block of one row; acc/mag start at 0 per row, blocks in order |
| `omega_gpu_fmt(out, cap, fmt, ...)` | `gpufmt out(1024 B) cap fmt(1024 B) args(16 ulong) pool(4096 B) → ulong` and `gpufmtkinds fmt kinds(64 ulong) → ulong` | see below |

Varargs cannot cross the ABI. Each argument a conversion consumes is one 64-bit word in `args`. An
`int`-sized kind uses the low 32 bits, which is what AAPCS64 `va_arg(ap, int)` reads, and `%c` uses the
low 8 bits. A `%s` argument is an offset into `pool`, where the string is NUL-terminated;
`2^64-1` stands for NULL and prints `(null)`. `gpufmtkinds` lists the `va_arg` type of each consuming
conversion: 1 int, 2 long, 3 long long, 4 ptrdiff_t, 5 unsigned, 6 unsigned long, 7 unsigned long long,
8 size_t, 9 const char *. With that list a C adapter only calls `va_arg` with the listed types and
never parses the format string. Both functions use the same scanner (`fscan`).

`parity/unit.py gpumath.om NAME` cuts the function-mode unit for `NAME`: the function and every library
function it calls, with `NAME` last. All 15 units compile with the pinned omc in function mode. The pinned
generator (`src/native.om` of omega-bridge `012860db`, sha256 `2c2c53f6…3940`) accepts them, and the C it
writes compiles with `-Wall -Wextra -Werror`, exporting `omega_hmlog` … `omega_gpufmtkinds`. In a
separate check (a /tmp driver, not in the tree), every corpus stream except the internal `fma` stream was
run through those generated bindings. The output was byte-identical to the C oracle's.

## Parity

Reproduce from a fresh clone (no C oracle needed; the oracle's output is recorded as sha256 in
`parity/expected.txt`):

```
OMEGA_OMC=/path/to/seed/out/omc [OMEGA_BRIDGE=/path/to/omega-bridge] sh om/gpumath/parity/run.sh
```

`parity/corpus.py` regenerates the corpus deterministically (splitmix64, fixed seeds, about 48 MB, never
committed). The run takes about 12 s on the Spark; the .om harness needs 2.5 s for all streams, the -O2 C
0.5 s.

Oracle: the three C files above at omega-mv `acb3f15c` (sha256 `4ee994d7…f1eb` hostmath.c, `10ae3b45…0d4b`
hostmath.h, `881daeaf…fe79` fp8_pack.c, `795e9620…1efa` fmt.c, `dd9f13c1…2ba8` fmt.h, `ddebec31…b668`
fp8_api.h, `7e3c1779…105a` codegen_moe.h) compiled with
`gcc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc`. `omega_bf16_to_fp32` is the same text cut
from `src/omega_blackwell_matmul.c` and built with the same flags. `.text` sha256: hostmath
`4061ecf7…6e2c`, fp8_pack `1cd43d3f…7cc3`, fmt `d06b83b7…92b7`. A /tmp driver feeds the corpus to
these objects. `fma` records use the hardware `fmadd` (inline asm), and the fmt records are typed C
calls that `corpus.py --c-cases` generates. Compiler: omc sha256 `734f0a39…6b61`; `gpumath.om` at this
commit compiles to a 29,836-byte program.

| stream | function(s) | inputs | result |
|---|---|---|---|
| fma | `fma64` (internal, vs hardware FMADD) | 326,561 triples: every pair of 27 special doubles with every third special as addend (`D_SPECIAL[::3]`, 9 addends, 6,561 triples), class mix, near-cancellation, exact ties, sticky-only ties, subnormal and overflow products, hostmath shapes | 326,561/326,561 identical |
| log | `hmlog` | 226,442: specials, every power of two 2^-1074..2^1023, 100,000 random bit patterns, 100,000 positive finite, near 1, around √2, 2^e(1+tiny) | 226,442/226,442 |
| exp | `hmexp` | 221,309: specials, 100,000 random bits, 100,000 in [-750, 712], ±200 ulps around both cutoffs, around k·ln2 for k = -1080..1024 | 221,309/221,309 |
| pow | `hmpow` | 100,097: RoPE θ^(±2i/d) for 7 θ × 5 d, random base/exponent, random bit pairs, special×special | 100,097/100,097 |
| sincos | `hmsincos` | 340,027: specials, 100,000 random bits, 100,000 with abs(a) < 2^20·π/2, 100,000 RoPE angles (pos < 65536), 30,000 within 4 ulps of nπ/2, small | 340,027/340,027 (sin and cos) |
| pow2f | `hmpow2f` | 12,006: every e in [-1000, 1000], the int extremes, 10,000 random ints | 12,006/12,006 |
| e4m3 | `e4m3ref` `e4m3kbits` `e4m3isnan` | all 256 bytes | 256/256 (×3 outputs) |
| scales | `f8packscales` | **all 65,536 bf16 × fold/no-fold** (131,072 single-element calls) + 60 pieces of 0..4096 with NaN/inf/overflowing scales (240,978 elements in total) | 131,132/131,132 calls, return value and every output word |
| xf32 | `f8packxf32` | 100,025 f32 (25 specials + 100,000 class mix: NaN payloads, ±inf, ±0, subnormals, random bits) + 777 again as an odd piece; 0-length piece | 100,802/100,802 |
| xbf16 | `f8packxbf16` | **all 65,536 bf16** + an odd piece of 333; 0-length piece | 65,869/65,869 |
| scan | `f8scannan` (4096-byte pieces) | 400 arrays, 0..20,000 bytes (3,889,100 bytes), NaN density 0 to all | 400/400 (count and first) |
| gemv | `f8gemvblock` (row × block) | 13 matrices, 2,432 rows, k up to 2048, NaN codes, special x and scales, a NaN×NaN-payload collision at every row's first NaN | 2,432/2,432 rows (y and yscale) |
| fmt | `gpufmt` `gpufmtkinds` | 28,688 calls: every format of length 1..4 over `%lzdusxcA` (two caps each), every modifier × conversion pair (`""` `l` `ll` `lll` `z` `zl` `lz` `zz` × `duxsc%qXi.5-`), 12,000 random mixes, caps 0..1024, NULL `%s`, int words with garbage upper halves | 28,688/28,688 (return, bytes up to the NUL + 1, va_arg kinds) |

Thirteen small mutants of `gpumath.om` are part of the verdict. The script applies each one, and each
must change its stream's output:

| mutant | records that change |
|---|---|
| ties round up instead of to even | fma 16,209 |
| sticky bit dropped | fma 15 |
| NaN priority of operands swapped | fma 1,332 |
| exact-zero sum keeps the sign | fma 10 |
| unfused `s * 2t + e*ln2lo` | log 134 |
| unfused first `sincos` reduction step | sincos 1,917 |
| `pow` multiply order swapped | pow 209 |
| `pow2f` normal-range bias off by one | pow2f 254 |
| e4m3 subnormal scale off by one | e4m3 14 |
| scale fold 2^55 | scales 51,175 |
| scan first index without the piece base | scan 15 |
| GEMV multiply order swapped | gemv 1 (the collision matrix) |
| `%c` prints `?` | fmt 2,008 |

Fused steps that did not affect any output on this corpus: `exp`'s `- k*ln2lo` step, `log`'s final
`e*ln2hi` step (the product is exact, since ln2hi has 33 significant bits), the last `sincos` step
(`n*pio2_3t`), and the `log` series step (one `pow` result changes, no `log` result). They stay fused
because the object fuses them.

## Limits

- The parity contract is the current object. If the C were built without contraction
  (`-ffp-contract=off`), its own results would change, and this port would keep matching today's
  object, not the new one.
- Fixed pieces, set by the ABI v1 array rule: 4096 elements for scan/scales/x packing, one 128-column
  block for the GEMV reference. The C adapter that loops over them is not written here. A NULL
  `yscale` is the adapter ignoring `acc 1`, and a k that is not a multiple of 128 is the adapter
  running only the k/128 whole blocks, as the C does. The committed corpus has neither case: both
  were checked once in a /tmp run against the C (k = 200, 328 and 127 with `yscale` NULL and
  non-NULL; y and yscale identical), not by `parity/run.sh`.
- `gpufmt` limits are limits of the .om, not cases where the C goes out of bounds: `fmt` at most
  1023 bytes plus its NUL inside 1024, at most 16 consuming conversions, `%s` strings inside a
  4096-byte pool, a 1024-byte `out`. Past the first three the .om traps 3 where the C keeps working
  (it takes as many varargs and as long strings as the caller passes). A `cap` above 1024 traps only
  when an output write actually passes index 1023 of `out`; shorter output with a larger `cap` runs
  normally. The committed corpus stays within 16 conversions; the exact 16-conversion boundary
  (cap 1024 and cap 40: identical to the C) and the 17-conversion trap were exercised only in a /tmp
  run, not by `parity/run.sh`. A NULL `out` or `fmt` cannot be expressed; the adapter keeps the C's
  `!out || cap == 0` and `fmt == NULL` checks.
- Narrowing that the C does by parameter type traps instead: an e4m3 byte above 255 traps 5, and so
  does a bf16 scale above 65535. `hmpow2f` takes a `long` where the C takes an `int`.
- `fma64` is internal and assumes 53-bit significands (`mul128`).
- Two omc gaps shaped the code: a function cannot return `f32` (error 5), so f32 helpers return their
  bits, and `const` is refused in function mode (error 9), so constants are written as their bit
  patterns inline with a note.

## NOT_RUN

- No GPU, model or serving test: nothing here runs on the device.
- No `make test`. The C files and every C caller are unchanged.
- The C adapters, and the removal of the three objects from `libomega_gpu.a` through omega-bridge #8, are
  not done here.
- Only the pinned omc on Linux arm64 (the Spark) was used.
