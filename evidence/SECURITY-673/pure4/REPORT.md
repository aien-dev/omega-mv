# Integrated strict-launch gate and independent-review corrections

This extends the existing native coordinator; no second compiler, launcher, authority or evaluator
is introduced. Production enforcement remains `seed/omc/omc.om` at265e19c0. The test source is
`seed/qualification/security673/gate669.om`, strict mode4; all new first-party automation is `.om`.
Existing bootstrap and host-suite shell remain historical migration prerequisites, not retired code.

Threat, protected assets and syscall boundary are detailed in `../pure1/REPORT.md`. This campaign
adds reproducibility from the reviewed checkout and tests actual native consumers and compiled
bypass controls. It is bounded Linux AArch64 testing. No GPU, hardware device or private input was
used. See platform.txt for the executed kernel. strace6.8 and GNU objdump2.42 are independent kernel
and instruction oracles in pure1 and independent review; the native coordinator shares seed lineage.

## Executed commands

Working directory: `/home/drakestapleton/workspace/omega-security673-reviewed`.

```
sh seed/omega-from-seed.sh
seed/out/omc < seed/qualification/security673/gate669.om > build/gate669-reviewed
readelf -h build/gate669-reviewed
chmod +x build/gate669-reviewed
build/gate669-reviewed 4
```

Each build/inspection must exit0 before chmod/execution. mode4 creates fresh `build/security673-native`
and `build/security673-pure`; existing directories cause refusal rather than evidence overwrite.
Between runs, scratch was renamed to the campaign suffix in the retained artifact archive.
`gate-reviewed.log` contains every raw compiler/child command, expected and actual exit, and timing;
`gate-reviewed.exit` is0 and stderr is empty. The gate builds both compiler stages through om4 and
requires identical bytes; this is reproducibility, not an independent DDC result.

Mode4 checks the actual public stdin/stdout result; synthetic private/judge files remain exact;
raw execveat/openat/getpid/mmap refusals are exact; removing seccomp exposes getpid (exit71), while
removing the inode fence executes the unassigned static fixture (exit77). The raw fixture generator
must reject211-,213-,512-byte input with92 and empty output. CPU exhaustion must terminate with
SIGXCPU24; the one1048577-byte output write must return1048576 and produce exactly that many bytes.

## Reviewer-driven verdict corrections

Linux wait status represents SIGXCPU24 with an optional core flag128. The first strict gate refused
its observed low byte152, exit99. This failed attempt is retained, not counted as success. The first
repair masked the core flag but had another error: normal exit152 could match the derived signal
result152. Independent review found that collision. The final `reap` requires a signaled wait when
`sigwant` is nonzero; any normal exit fails. The raw low byte is still logged.

`gate-child.om` mode3 deliberately returns152 normally. `build/gate669-reviewed 5` must return99;
`signal-spoof.log/.exit` records that refusal. The exact-once mode6 source transform removes only
this normal-exit rejection from the coordinator. This weakened native coordinator runs the same
mode5 and returns0, demonstrating the control reaches the intended defect. Its commands were:

```
seed/out/omc < seed/qualification/security673/branch-mutate.om > build/mutator-reviewed
build/mutator-reviewed 6 < seed/qualification/security673/gate669.om > build/gate-spoof-mutant.om
seed/out/omc < build/gate-spoof-mutant.om > build/gate-spoof-mutant
build/gate-spoof-mutant 5
```

Builds and ELF inspections returned0 before execution. See spoof-mutant.log/.exit for the observed0.
The mutant is a qualification artifact only.

The earlier supervisor controls0/1/2 also unconditionally returned99 after `run`. That could mask
an incorrectly accepted result. This forced failure was removed: an incorrectly accepted child now
returns0, while actual wrong-exit/signal/deadline refusal remains99. Fresh control receipts preserve
their individual exits and log reasons. Earlier control99 observations are historical and were not
alone adequate evidence of verdict sensitivity; the earlier independent review missed this issue.

## Failures, regression and identities

Early coordinator builds13 and12 were invalid fixtures due to identifier/register limits. Their
stderr is retained; none was executed as a successful build. `build-gate.err`, `build-gate-2.err`,
`build-gate-4.err` identify those failures. The first SIGXCPU gate99 and pre-review gate observations
are retained separately. Only the corrected reviewed snapshot supports final test claims.

Branch-form/packet, hostile-source and existing host regressions each exited0; exact receipts are
regression-*.log/.err and regression-exits.txt. Source, compiler, runner and scratch identities are
in identities.txt. `artifacts.tar.gz` preserves generated source, binary, output and stderr. Hashes
are identifiers, not correctness proofs. Independent replay is recorded separately.

## Residual assumptions and blocking boundary

The host has a piped Apport core handler (core-pattern.txt); RLIMIT_CORE0 does not prevent a piped
handler from processing a crash. The SIGXCPU core flag is not proof that a dump persisted, and no
claim of upload is made. Private-memory/holdout confinement is NOT qualified on this environment.
The existing runtime/host owner must provide a disposable environment with independently checked
crash collection policy before private input is admitted. No live host policy was changed.

Stdio remains caller-assigned authority; executable inode pinning does not seal content. A trusted
parent must protect artifact bytes, handle pipe-output and wall limits and collect outcomes. This
profile does not authenticate grants, implement revocation fences or durable action/evaluation
budgets. Those acceptance criteria remain open in the existing owners. Test success does not prove
arbitrary binary correctness, kernel correctness or whole-system security.
