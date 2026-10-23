# Corrective branch qualification

Scope: #669, with bounded compiler-input and test-verdict checks relevant to #671/#624.
This receipt does not close #673, qualify a sandbox, or approve release.

Threat model: source input can require a branch beyond its encoding range. The protected asset
is control-flow integrity of the emitted program, and accurate acceptance/refusal by the compiler.
The demonstrated defect was wrong branch encoding, not demonstrated data theft or host compromise.
Enforcement is `seed/omc/omc.om:disp19`, consumed by `patch19` and `blo`, on Linux AArch64.
The corrected backward unsigned boundary is2^64-1048576. Both exact signed extrema are accepted;
both adjacent out-of-range values and both alignment controls refuse24. Current callers either
use that guard or the documented aligned8MiB imm26 capacity invariant.

`native-gate6.log`, `.err`, `.exit`, `source-identities.txt`, and `native-gate6-identities.txt`
are the final author result for this slice. `native-gate6-artifacts.tar.gz` retains every generated
source, compiler/probe/program binary, parameter fixture, decoder output and child diagnostic.
The numeric filenames are linked to raw commands in the log. Source identities include the gate,
helpers, om4, rebuilt compiler source, runner binary and external decoder. Platform/version files
identify the actual native host and decoder. The archive is synthetic and contains no private data.

Properties tested: self-rebuild equality; eight exact encoder boundaries; real backward array fill;
small and maximum forward programs decode to the marker and execute0; oversized if, nested if,
while and trap-site programs refuse24 with empty output; a compiled range-bypass mutant emits
the wrong target, which the external decoder plus checker rejects61. That malformed program is
never executed. Seven decoder-text controls cover a valid marker, malformed target, wrong register,
duplicate marker, absent marker, wrong target and mnemonic-prefix confusion. Empty input refuses62.

Supervisor controls in `control-*.log`, `.err` and `control-exits.txt` exercise ordinary wrong exit,
self-signal and deadline overrun; all returned99. Signals/timeouts are reported INVALID within the
failed run, not PASS. The child wait low byte9 is recorded for signal/deadline controls. There is no
process-tree or kernel resource isolation claim.

Independent review found an overly permissive marker/target parser. The final source adds token
delimiters and an exact pinned-format x1 marker, with the review reproducers in the native corpus.
The independent final review is recorded separately and must match these source identities.

Failed author attempts remain in this directory. Initial mutator/checker builds failed16,12,17;
gate builds failed13,3,18,7. These are failed development attempts, not security passes. The first
probe run mistakenly executed empty failed-build outputs through Bash, producing misleading0s.
`probe-results.txt` and initial probe files are INVALID and excluded. Subsequent execution requires
successful compilation and ELF inspection, and the native coordinator independently checks ELF
magic before execution. Gate attempt1 failed105 in fixture-byte narrowing; attempt4 failed99 when
the array fixture did not compile. Both were corrected and rerun. Other earlier passes are
superseded by final-source gate6, not silently substituted for it.

Historical preservation: the seven `evidence/SEED-0/omc` files replaced by the initial candidate
are restored exactly from f0499f41. DeepSeek's replacement bytes remain separately under
`deepseek-gate5-preserved`. The retired shell669 gate remains as historical text, as well as in
its original Git commit. Its existing report and crash-inclusive log remain historical evidence
and are not the authority for this corrected slice.

Residual assumptions: trusted Linux kernel, GNU decoder and filesystem; no concurrent source or
artifact modification by another process under the same account; same-lineage checker/runner;
fixed synthetic input families; no complete language proof, diverse compilation, timing-channel,
device-isolation or arbitrary-native-code sandbox claim. Existing seed reconstruction still uses
historical shell tooling. Closure requires the compiler/release owners' integrated review.

Raw invocation for the final author run:

```text
build/security673-correction1/stage2 < seed/qualification/security673/gate669.om > build/security673-correction1/gate669 2> evidence/SECURITY-673/correction1/gate-build10.err
exit 0
readelf -h build/security673-correction1/gate669
exit 0
chmod +x build/security673-correction1/gate669
exit 0
build/security673-correction1/gate669 > evidence/SECURITY-673/correction1/native-gate6.log 2> evidence/SECURITY-673/correction1/native-gate6.err
exit 0
```

Gate child commands and every actual exit are in the raw log; supervisor-control commands use
the same executable path followed by0,1,2, with fresh scratch and respective logs. They exited99.
