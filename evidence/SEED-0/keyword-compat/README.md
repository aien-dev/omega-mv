# Keyword compatibility audit (rung R13)

Question: did adding the float words to the `.om` keyword table break existing programs?

Corpus: every `.om` source under `benchmarks-om/om/` and `benchmarks-om/tests/om/` (103 files) in
the benchmark workspace. These are the authored benchmark programs, not generated copies.

Reference compiler: the benchmark-pinned compiler, rebuilt here from `seed/omc/omc.om` at commit
`f0499f41` using `seed/rung4/om4.bin`. Its SHA-256 is
`e5d60db892c8f21cc8c9128af26d8bded0e96d7f92946f5a549397c43a872649`, matching the pin in
`benchmarks-om/docs/OMEGA_REWRITE.md`.

Method: for each file, compile with the reference compiler and with `seed/out/omc`; compare the exit
status and the output bytes. A signal exit (>= 128) counts as a crash.

Before the rename (words were `bits`, `sqrt`, `abs`, `min`, `max`): 103 files, 7 differences, all
new refusals (error 16) where the old compiler reported 3 or 9. Six files used `bits` as a parameter
or variable; one used `min`. No crashes.

After the rename (words are `bitcast`, `fsqrt`, `fabs`, `fmin`, `fmax`): 103 files, 0 differences,
0 crashes. Every existing program compiles to identical machine code.

The audit is a one-off over an external corpus, so it is not a gate in `seed/host/check.sh`. The
property it protects is pinned in-tree by `seed/omc/gate11.sh`, which requires the old short
spellings to remain usable as names and the new words to be reserved.
