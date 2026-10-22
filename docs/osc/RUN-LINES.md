# Run lines in the OSC compiler tests

`tests/compiler/test_osc_compiler.c` runs two kinds of hand-written lines. Each gives a function, its
arguments and the result both engines (interpreter and native) must produce.

| key | where | form |
|---|---|---|
| `// expect-run: ` | `tests/compiler/progs/*.osc` | `ENTRY ARG... -> VALUE` or `-> trap=NAME` |
| `// slice-run: ` | `tests/compiler/slices/*.osc` | `ENTRY ARG... -> VALUE` or `-> trap=NAME`, then optionally `cells=v,...` |

Arguments:

- a scalar: a decimal, optionally negative (`-1` is all ones in an unsigned parameter);
- `bytes:HEX`: an even number of hex digits (`bytes:` is empty, `bytes:NULL` is a null slice);
- `cells:N` (N zeroed cells) or `cells:N=v,...` with exactly N decimals; `cells:NULL`.

`trap=NAME` must name a real trap (`BOUNDS`, `REQUIRES`, ...). Nothing may follow the result except
the `cells=` list of a `slice-run` line, which must have one value per cell of the first cells argument.

## Every line runs or fails

- **No length limit.** Each line is copied to the heap whole. The runners once copied lines into a
  512-byte buffer and skipped any line of 400 characters or more without a word, so a long fixture
  passed by never running. A cubin image is thousands of hex digits, so this mattered.
- **Strict tokens.** A malformed token fails the run: odd-length or non-hex `bytes:` data, a number with
  trailing characters, a value out of range, a wrong cells count, an unknown trap name, extra tokens.
  The old parser read `bytes:zz` as zero bytes and `12abc` as 12.
- **Counted.** Before running a file, the harness counts its key lines separately from the runner. After
  the runner it checks that the two counts match (`N slice-run lines, M handled: lines were skipped`).
  A line that starts with the key's stem in any other form (indented, no space after the colon) fails
  as malformed instead of being ignored.
- **Self-test.** `run_line_selftest` feeds the grammar good and bad tokens before any program runs, so a
  weakened check fails on its own.

The summary prints `run lines: slice-run keyed=K ran=R  expect-run keyed=K ran=R`. On a passing run
both pairs are equal.

The negative controls (cap put back, hex check removed) and their results are in
`evidence/L401-cubin-reader/README.md`.
