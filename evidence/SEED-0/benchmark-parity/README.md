# Benchmark parity audit (rung R15)

Question: does the current `seed/out/omc` compile and run the real benchmark workspace exactly as the
benchmark-pinned compiler does?

Reference compiler: rebuilt from `seed/omc/omc.om` at commit `f0499f41` with `seed/rung4/om4.bin`;
SHA-256 `e5d60db892c8f21cc8c9128af26d8bded0e96d7f92946f5a549397c43a872649`, matching the pin in
`benchmarks-om/docs/OMEGA_REWRITE.md`.

## 1. Every authored source (103 files)

Every `.om` under `benchmarks-om/om/` and `benchmarks-om/tests/om/` was compiled by both compilers and
the exit status and output bytes compared: **103 files, 0 differences, 0 crashes**.

## 2. Every build-plan target (53 targets)

Each line of `benchmarks-om/om/build.plan` names a target and its ordered source units. For each
target the units were concatenated (the workspace's supported composition mechanism today) and
compiled by both compilers: **53 targets, 0 differences, 0 crashes**. This covers `om-build`,
`prime-reference`, `prime_race`, the JSON/data tools, the stream and measure tools and every test
target, including multi-thousand-line concatenations.

## 3. Runtime behaviour (`prime-reference`)

`prime-reference` was built from `om/prime/reference.om` + `om/prime/main.om` with the current
compiler and compared with the pinned `build/om/prime-reference` on requests
`p 97`, `p 1000000007`, `p 561` (pseudoprime), `c 1000`, `c 100000`, `b 64`:
**identical stdout, stderr and exit status on all six**, including raw bitmap bytes.

## Limits

This is evidence about the benchmark workspace at the time of the audit, not a permanent gate: the
corpus is external. The properties it protects that live in-tree are gate11 (reserved-word
compatibility) and the om4/omc corpus in `seed/host/check.sh`.
