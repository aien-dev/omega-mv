# Strict-profile bypass and fail-closed controls for Omega #670

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-qualified`.
Base: `8dfe778` plus the control sources in this commit. Platform: Linux AArch64, native execution.

## Purpose

The strict `execpure` profile is only meaningful if removing an enforcement step produces the
expected bypass, and if an unavailable kernel operation refuses the launch. This campaign compiles
controlled mutants of the actual compiler, rebuilds the launcher from each, and checks the exact
exit or signal.

All mutants are qualification artifacts. None is a production compiler.

## Controlled mutations

`branch-mutate.om` performs exact single-substring transforms on `seed/omc/omc.om`:

| Mode | Transform | Removed control |
| --- | --- | --- |
| `2` (ASCII 50) | `hsys 436` -> `hsys 999` | close_range unavailable, must fail closed |
| `4` (ASCII 52) | `hsys 277` -> `hsys 172` | seccomp filter installation replaced with getpid |
| `5` (ASCII 53) | `hsys 446` -> `hsys 172` | landlock_restrict_self replaced with getpid |

The transform is exact once; the mutator refuses if the needle is absent or appears more than once.
The mutated source is compiled by the same rebuilt compiler, then `pure-launch.om` is rebuilt with
the mutant. The resulting compilers are byte-identical to the earlier candidate artifacts for modes
4 and 5.

## Results

| Control | Expected | Observed |
| --- | --- | --- |
| fixed-other: normal launcher, target exec from raw fixture | Landlock denies with EACCES; fixture exits 0 | 0 |
| bypass-filter: seccomp mutant, raw getpid fixture | getpid succeeds; fixture exits 71 | 71 |
| bypass-inode: Landlock-restrict mutant, target-exec fixture | target executes; fixture exits 77 | 77 |
| cpu: normal launcher, deliberate CPU exhaustion | SIGXCPU from the 1 s limit; exit 152 | 152 |
| output: normal launcher, 1,048,577-byte write to a regular file | write returns 1,048,576; file size 1,048,576; exit 0 | 0, 1,048,576 bytes |
| unavailable close_range: mode 2 mutant | ENOSYS (38), `PURE_LAUNCH_REFUSED`, no child | 38 |

`fixed-other.strace` shows `execveat(..., pure-other, ...) = -1 EACCES`.
`bypass-filter.strace` shows `getpid()` succeeding because no filter was installed.
`bypass-inode.strace` shows the target `execveat` succeeding because the Landlock restriction was
replaced.
`output.strace` shows `write(..., 1048577) = 1048576` followed by `exit(0)`.

The author evidence in `../pure1` also records unavailable landlock_create_ruleset = 38,
seccomp = 13 and prlimit64 = 1. Those three rows were not independently replayed in this pass; they
remain author evidence, not reviewer-confirmed results.

## Residual limits

- Removing a control proves the control is reachable and meaningful for these fixtures; it does not
  prove there is no other bypass or kernel defect.
- The profile remains fixed. Grant identity, generation, revocation and durable budgets are still
  open under #670/#649/#651.
- The mutants and fixtures share the seed compiler lineage. No diverse compilation or formal proof
  is claimed.
- Only synthetic files and native fixtures were used. This does not close #670 or #673.
