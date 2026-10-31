# sm121dis: Omega's own GB10 disassembler

A disassembler turns the GPU's machine code back into readable instructions. Omega used to borrow NVIDIA's
`nvdisasm` for that, in every check that reads a kernel back. `sm121dis` is our own (row C6 of `docs/OMEGA.md`):
the decoder is `src/omega_sm121_disasm.c`, this folder holds the command line and the comparison with NVIDIA's tool.

## What it proves

- Every kernel the library builds without the chip (each GPU test in its host-only and simulator modes, the
  numeric kernels, and every kernel the hazard checker's sweep builds) is collected and compared word by word with
  `nvdisasm`, text and spacing included. All 2,980 kernels (1,218,992 instruction words) agree (2026-10-30, host).
  The receipts in `evidence/C6-DISASM/` predate the sweep kernels: 331 kernels (59,152 words) on 2026-10-24, 402 kernels
  (74,096 words) on 2026-10-25.
- A copy of the decoder with one planted fault (`SM121DIS_MUTANT`, first source register off by one bit) must fail
  that comparison with a verdict, and does on 2,979 of the 2,980 kernels. The one it cannot touch is a sweep kernel of
  32 words that is only EXIT, a branch and padding: it has no source register for the fault to change.
- A kernel that could not be saved whole (a full disk) fails the collection; it is never compared cut short.
- A word the decoder does not know is printed as `?? <hex>` and fails the check. It is never guessed.

## How to run it

    make test-sm121dis          # collect, compare with nvdisasm, then the red check

`nvdisasm` is used only here, as the reference to compare against. Nothing it prints goes into a kernel.

## Who uses it

The checks that used to call `nvdisasm` now call ours:

- the listing checks inside `tests/gpu_elementwise_test.c`, `gpu_fp8_api_test.c`, `gpu_reconv_test.c`,
  `gpu_attention_test.c` and `bench/prime_race/tests/gb10_sieve_host_test.c` (linked in, `omega_sm121_listing`);
- `tools/divsqrt_nvdisasm_check.sh`, `tools/ldst_nvdisasm_check.sh` and `make test-numeric-reduce-nvdisasm`
  (the `build/sm121dis` command);
- the hazard checker's register cross-check, `tests/bw_hazard_test.c --disasm` (`make test-bw-hazard-nvdisasm`,
  linked in, `omega_sm121_disasm`), over every kernel its sweep builds. Those kernels are in the comparison corpus.

The script and target names are kept so older receipts still line up. `make test-zero-nvdisasm` proves that none
of these checks runs `nvdisasm`.

## Files

| File | Job |
|---|---|
| `sm121dis.c` | command line: a listing, `-b SM121` (called like nvdisasm), `--pairs` (for the comparison), `--version` |
| `corpus.sh` | runs every GPU test with `OMEGA_KERNEL_DUMP_DIR` set, so each kernel the library encodes is saved |
| `check.sh` | compares our text with nvdisasm's for every saved kernel, `SM121DIS_JOBS` at a time (default: every CPU the process may use); PASS only when every word agrees |
| `oracle_pairs.sh` | turns nvdisasm's listing into the same one-line-per-word form |
