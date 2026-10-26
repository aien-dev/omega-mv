# Native executable contracts qualification

EXERCISED on the arm64 Linux host. This is not GPU qualification or a claim that all Omega 1.0 milestones are complete. PR: #807; guide update: aien-dev/omega-harness#27 (pending at collection).

## Exact source and compiler

- Tested source: `00031d2456b2fe1e6ecdee6a0245d4c27b6749d2`.
- `seed/omc/omc.om`: SHA256 `0df1a31623f3aabadac6c7661744bf8a2f3a0d31a313639909001579a962e1ec`.
- Compiler: SHA256 `3d2b7411b8a2ba1a3e770e70fd7c9be22100357c51206f2e90a80df826c30fa3`.
- Source has 11,555 lines and 390 functions; emitted compiler is 309,292 bytes. The fixpoint built by om4, stage1 and stage2 is identical.
- Word bookkeeping: `WolframLanguageEvaluator: (309292-176)/4 -> 77279`; 176 bytes are the ELF header, the remainder AArch64 instruction words. This is size bookkeeping, not a speed result.

## Executed checks

A fresh local clone of the committed source ran, under `nice -n 19 ionice -c3`:

```
make -j2 build/omegese/omegese build/omega_emit/omega_emit build/checker2/checker2
sh seed/host/check.sh
sh seed/omc/gate5.sh
```

The combined process exited 0; `qualification.log` retains the output. The C comparison tools were built before the host script, so its IR path was exercised, not skipped. The unchanged C compiler/runtime oracle was used for the contract function's exact value/bit/trap comparison. The fixture inputs include both sides of the requires/ensures bounds. C source was not modified by this milestone.

- Contract conductor: `cases=44 pass=44 fail=0 invalid=0 not_run=0`.
- Two independent mutant compilers completed their test programs with exit 0 instead of required exit 109; stdout/stderr matched. The conductor correctly returned 1 and reported `cases=2 pass=0 fail=2 invalid=0 not_run=0`. One mutant erases enforcement; the other falsely proves every constant bool. A crash is not counted as detection.
- Frozen OM-1 C2 manifest: `cases=5876 pass=5876 fail=0 invalid=0 not_run=0`, including byte hashes and refusal diagnostics. `c2.log` records compiler identity and every verdict. This run used the exact source/compiler above before the commit was created; commit creation did not change the tested bytes.
- Native host qualification and its existing negative/regression controls passed.
- Referee gate5 completed 177 checks: seed fixpoint, om4 corpus, C referee, independent wrong-word mutants, deterministic output, nextfuzz and all historical error numbers 1..23. `GATE5 PASS` is the final line.

The existing Omega conductor rejects malformed or unknown rows, accounts INVALID and NOT_RUN separately, and refuses an empty case set. The reported denominators are its complete manifest counts, not a filtered set of successful rows.

### Actual foreign binding

The pinned omega-bridge source `431fff0a95f84c0a8bf9d3383f94d7e1f02d913e` generated an actual
shared library from `seed/omc/contracts/bridge.om`. Its generator compiler was the required
historical `734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61`, extracted from
Omega commit `e17039d9ceeb30e0637ef4145e37afc5821c3773`; `OMEGA_NATIVE_OMC2` was the tested
contracts compiler above. No generator pin check was bypassed.

```
OMEGA_NATIVE_OMC=/path/to/734f0a39/omc \
OMEGA_NATIVE_OMC2=/path/to/3d2b7411/omc \
  tools/native OUT /path/to/seed/omc/contracts/bridge.om
python3 seed/omc/contracts/bridge-check.py OUT/libomega_native.so
python3 seed/omc/contracts/bridge-check.py OUT/libomega_native.so --wrong-expectation
```

The checker is a verification-only Python ctypes caller, not a production implementation.
The real `omega2_check` export returns status 9 for input -1, status 0 with the expected
value for 0, 1, 9 and 10, and status 10 for 11 and 20. Failed calls leave the result sentinel
42 unchanged; a subsequent input 1 succeeds. Normal checker exit is 0; the deliberately
wrong expected status for input -1 gives exactly one mismatch and exit 1. `bridge.log`
retains both runs. `bridge-artifacts.tar.gz` retains generated declarations, code and library.

The initial attempt to bind the C-parity fixture's `int` result was correctly refused by
this bridge revision, whose scalar return subset is `ulong`, `long` or `void`. The explicit
`long` fixture tests that supported ABI without claiming that the bridge accepts every
native scalar signature. That broader limitation remains RM7.

## Gate5 receipt-path repair

Gate5 now writes `build/seed/r5/evidence`, not the frozen `evidence/SEED-0/omc` tree. The living `seed/omc/size-history.tsv` retains the historical 1,037-byte prefix byte-for-byte (SHA256 `cd0c201cd65fc560640be4d27e753d1655556a08e5801ff8f45748c16d0b076a`). It appends observed compiler rows; it does not auto-accept a new digest.

The exact committed last-row predicate was separately executed against the current, missing, empty and stale-last-row ledgers. Results are in `ledger-controls.txt`. In particular, putting the correct digest on an earlier row did not satisfy a wrong final row. After the full fresh-clone run, `git diff --exit-code <tested-source> -- evidence` returned 0 and `git status --porcelain -- evidence` was empty. No historical evidence was rewritten.

The removed source-prefix size diagnostic compiled incomplete programs whose forward signatures made its per-function sizes invalid. Whole-compiler measured size, compile-time collection, every correctness check and the last-row digest gate remain. Gate5's incidental timing table includes process startup and is not the preregistered efficiency milestone or an optimization claim.

## Artifacts and hashes

`artifacts.tar.gz` contains the fresh run's `build/seed/r5/evidence` and `build/seed/contracts`, including compiler mutant logs, C/native parity rows, case outputs and binaries. Paths printed inside the logs are the original execution paths; this archive preserves their payloads.

```
d0e4f2e37064f439fca07dabeabeed8e271de6071996746aef1288903fbb74c9  artifacts.tar.gz
0707f0b2786ad8fc2a3278ee76bae4599d05712e81d45556bf563e4366ac7d70  c2.log
6d2c2d2178e7beb2c6fc4cf36b9771c40578a76c43bd4f8278822a95c1d2565a  mutants.log
9d6316d82435f165392168cf5ba72ea03053a2665e962a00d78f6c56a446fa81  qualification.log
```

## Scope and remaining gates

Contracts are opt-in native OM-2 syntax. Debug is the default; release still checks syntax/types/purity before erasing predicates. Constant-true proof is distinguished from release policy. Requires/ensures/invariant runtime errors are existing traps 9/10; malformed or impure contracts are compiler error 30. IR rejects these source contracts rather than silently dropping them. C2 OM-1 bytes remain unchanged.

This does not qualify a new IR memory backend, GPU targets, a complete contract theorem prover, or host contracts in omega-bridge. The frozen C oracle remains an independent comparison implementation. PR #789's protected GPU-gate work remains BLOCKED_OPERATOR and was not bypassed. The separate efficiency experiment has no candidate result in this receipt.
