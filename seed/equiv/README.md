# irequiv: exhaustive equivalence of OSC IR scalar functions (issue 826)

`irequiv.om` is seeded Omega. It reads one OSC IR v1 unit (`spec/one-layer-format.md`) on stdin,
interprets its functions with the semantics of `src/compiler/osc_interp.c` (wrapping unsigned
arithmetic, signed overflow, division, shift and conversion traps, explicit `trap N`), and compares
two functions over every input of their shared signature. Two results are equal only when both
return the same bits or both trap with the same code.

```sh
seed/out/omc < seed/equiv/irequiv.om > irequiv && chmod +x irequiv
{ printf 'ir\n'; cat seed/equiv/pop16.unit.om; } | seed/out/omc > pop16.ir
./irequiv check pop16 pop16s < pop16.ir      # EQUIV PASS ... domain=65536 checked=65536 differing=0
./irequiv check pop16 pop16e < pop16.ir      # EQUIV FAIL ... differing=1 first=ushort:65535 ...
./irequiv sample A B N SEED < unit.ir        # wider domains: EQUIV SAMPLED ... NOT_A_PROOF, exit 10
./irequiv eval F uchar:1 uchar:9 < unit.ir   # value:N or trap:C
./irequiv dump F < unit.ir                   # 8 LE bytes per input, enumeration order
sh seed/equiv/gate-irequiv.sh
```

- `check` needs identical parameter and return types and at most 24 parameter bits (bool counts 1);
  inputs are enumerated with the last parameter in the low bits. The whole domain is scanned; the
  line reports the count of differing inputs and the first one with both results.
- `sample` draws N inputs (at most 67108864) from xorshift64* seeded by SEED. Agreement exits 10,
  never 0, and prints `NOT_A_PROOF`.
- Each evaluation has 16777216 instructions of fuel; running out prints `EQUIV INCONCLUSIVE`
  and exits 6, never PASS or FAIL.
- Exit codes: 0 PASS, 1 FAIL, 2 usage, 3 unknown function, 4 signature, 5 domain over 24 bits,
  6 fuel, 7 trap during `dump`, 10 sampled agreement, 20-28 the IR reader's refusals.

Lines 43-496 are the reader and validator of `seed/wasm/irwasm.om`, copied verbatim; the gate
fails if the copy drifts from irwasm or irspirv. Fixtures: `hexv` (js_hexv from seed/json/json.om,
hhexv from seed/net/http.om), `clamp` (dev01 clamp_add), `bitrev` (dev09 bitrev8), `rot` (r8 from
seed/omc/om2/s1/rot.om), `pop16`, and `wide` (sampling and fuel). Each names its source and its
deliberate mutants.

The gate also runs every row of `seed/wasm/cases.tsv` (129), `seed/spirv/regression.tsv` (8) and
`seed/spirv/demo.tsv` (3) twice: as a native program built by `omc` with the irwasm/irspirv gates'
64-bit result dumper, and through `irequiv eval` on the unit's IR. It requires the same bits or the
same trap code on all 140 rows (`CONFORM native rows=140 equal=140`), so an interpreter defect that
no fixture pair reaches still fails the gate.

Limits: OSC IR v1 scalar functions only (what `omc` IR mode emits: at most 32 functions, 6
parameters); the result is a statement about the IR both functions lowered to, not about the
AArch64 words the native path emits for the same source.
