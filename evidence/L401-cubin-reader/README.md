# L401: a CUDA image (cubin) reader written in Omega

The CUDA++ swap path (L401) has to open CUDA images that users build with their own toolchain. Those
images are untrusted input. This reader parses them in Omega (OSC), with no C in the program path. Every
read goes through a bounds-checked byte slice (`b[i]` traps `BOUNDS` past `b.len`), and every offset is
checked before it is used. Wolfram Language derived the expected answers independently and is never
part of the program.

The reader is `tests/compiler/slices/cubin_elf.osc`. The file has 117 run lines, then the code:

| function | returns |
|---|---|
| `u16_at`, `u32_at`, `u64_at` | little-endian reads; trap `BOUNDS` past the end |
| `hdr_ok` | 1 for an ELF64, little-endian, version 1, CUDA (machine 190) executable with CUDA ABI version 7 or 8, 64-byte header and section entries, and a section table inside the image |
| `arch_of` | the SM number: `e_flags` bits 8..15 under ABI version 8, bits 0..7 under version 7. All ones for a refused image |
| `layout_ok` | 1 when the string table is type 3, non-empty and inside the image; every name starts inside it; and every section with file bytes (any type but 8, NOBITS) lies inside the image and overlaps neither the ELF header nor the section table |
| `text_count` | kernels: PROGBITS sections named `.text.*`. All ones for a refused image |

Offsets are checked by `fits(n, o, z)` = `o <= n && z <= n - o`, which never forms `o + z`. An image can
set an offset near 2^64 so that `o + z` wraps to a small number, so the naive check would accept it.

Scope: this parses and validates the image on the host. It does not read kernel parameters
(`.nv.info`), symbols or relocations, does not launch anything on the GPU, and is not yet wired into
the swap library.

## Where the expected values come from

1. `gen_fixtures.py` writes 25 synthetic images: a good one, ABI 7, SM 90, NOBITS, two kernels,
   near-miss names, and refusals (bad magic, class, endianness, machine, ABI, entry size, zero
   sections, huge and wrapping table offsets, string index, string table type, name offset, section
   past the end, a section inside the header, a section inside the table, a wrapping section, a
   truncated file, an empty file). It does not compute any answer.
2. The real image is `primes-race/oracle/ops.cubin`: `nvcc -arch=sm_121 -cubin` of our prime-race
   `ops.cu` encoding oracle, 16136 bytes, sha256
   `271269535a32ef2b94d18a51e617e2e22e7c4deb7c93c44b776d6fd6c98f3e50`. It is the kind of input a
   CUDA++ loader reads, used here as test data only. No NVIDIA code is in any program path.
3. `wolfram_decode_v2.wl` decodes every image in the Wolfram evaluator. Bytes travel inline as base64,
   and the SHA-256 prefix Wolfram computes is compared with the file's: 25 of 25 and 3 of 3 regions
   match. Results are in `wolfram_v2.json`: the real image is valid, arch 121, layout valid, 5 kernels
   (`.text.k_hi2`, `.text.k_hi`, `.text.k_shl1`, `.text.k_shl`, `.text.k_or`).
4. `build_osc.py` writes the run lines from `wolfram_v2.json`, followed by the reader
   (`cubin_reader_body.osc`). Reassembling from these files reproduces the tested file byte for byte.

The independent decode caught three bugs before the reader ran:

- The test harness skipped every run line of 400 characters or more without a word. It copied each
  line into a 512-byte buffer and filtered longer ones out. 96 of the 117 lines are that long. See
  `docs/osc/RUN-LINES.md` for the fix.
- The first generator placed `.text` inside the section table. The Wolfram decode found the overlap.
- The generator and the first decoder both read the ABI version from `e_ident[7]`. It is in
  `e_ident[8]` (`e_ident[7]` is EI_OSABI, 0x41 on this image). Generator and decoder agreed with each
  other. The real sm_121 image showed both were wrong.

## Red, green and negative controls

`matrix.sh` builds four harness variants from the commit and runs each at the quick fuzz count (120)
against four readers: `stub` (every function returns 7777777), `real`, and two planted bugs from
`make_reader_mutants.py`. Log: `matrix.log`.

| run | harness | reader | result | what it shows |
|---|---|---|---|---|
| A | before (400 cap) | planted bug: ABI ignored | PASS, 86 of 182 slice lines ran | the old harness passed a wrong reader |
| B | after | planted bug: ABI ignored | FAIL, 12 lines (every ABI 8 image's arch) | the long lines now run |
| C | after | stub | FAIL, 117 of 117 lines, each on its value | red baseline |
| D | after | real | PASS; slice-run 182/182 ran, expect-run 143/143 ran | green |
| E | after, cap put back | real | FAIL: `117 slice-run lines, 21 handled: lines were skipped` | the line count catches a skip |
| F | after, hex digit check removed | real | FAIL: self-test accepted `bytes:zz`, `bytes:0g` | the grammar self-test catches a weaker parser |
| G | after | planted bug: naive `o + z <= n` | FAIL, 4 lines: the wrapping section passes layout, the wrapping table passes the header with arch 121 | the wrap fixtures catch the naive bound |

In D the 47 golden programs and 138 negative programs pass unchanged, so the stricter grammar
rejected no existing line.

## Gate

`make test-compiler` on the Spark at this commit: `RC=0`, `test-compiler: PASS` (log:
`gate-test-compiler.log`). Both the plain and the ASan/UBSan legs of the compiler harness print
`OSC1_COMPILER_PASS golden=47 neg=138 diff_runs=141032` and
`run lines: slice-run keyed=182 ran=182  expect-run keyed=143 ran=143`, with 29000 slice fuzz runs
where the interpreter and native code agree. The ASan leg also covers the harness's new heap copies.

## Reproduce

```sh
# on the Spark, in a worktree of this commit
python3 evidence/L401-cubin-reader/gen_fixtures.py            # images (fixtures.json)
# decode each image with wolfram_decode_v2.wl (inline base64), record wolfram_v2.json
python3 evidence/L401-cubin-reader/build_osc.py real tests/compiler/slices/cubin_elf.osc
make test-compiler
```
