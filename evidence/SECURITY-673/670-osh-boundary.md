# Omega #670 qualification: inherited descriptors at the OSH host boundary

Date: 2026-10-23. Parent tracker: #673. Branch: `fix/security673-branch-range`.
Base commit: `f0499f415571bdd7496b6de391096915e78f7556`.
Status: **PASS for unintended descriptor closure across spawn. The rest of #670 is NOT_RUN or BLOCKED on the existing
authority/store integration owners.** This is a bounded fix to the existing OSH launcher, not a new authority service.

## Threat models and the boundary that currently protects each

| Model | What it can do | Current boundary | Honest status |
|---|---|---|---|
| Compiler-produced restricted `.om` | Run emitted AArch64 with the process's Linux permissions | Seeded compiler memory/branch checks (see 669); native host words are explicitly **not** a sandbox | Memory checks only; no host containment |
| Arbitrary native code started by OSH | Anything the Linux user can do | OSH gates the `spawn`, redirection `open` and `chdir` it performs against `rx_caproot` grants; child runs with the user's full authority | Partial mediation; child not contained |
| Malicious dependency loaded by a child | Same as the child | None beyond the Linux user | NOT addressed |
| Compromised agent driving the shell | Can ask OSH for allowed effects | OSH policy is parsed before the first command and the admin handle is sealed; a compromised agent cannot mint after seal | Policy boundary exists; it is not a sandbox for spawned code |

The trusted boundary is the OSH host process plus the separate `rx_caproot` process. The seeded `.om` path has no
launcher: `seed/omc/omc.om` exposes `fork`, `execprog`, `pipefds`, `dupfd`, `fclose` and file/socket words, and
`docs/one_layer/OMEGESE.md` section 10 states they use the process's existing permissions.

## Existing enforcement planes that #670 must reuse

| Plane | Implementation | What it enforces | What it does not |
|---|---|---|---|
| Linux host (main) | `src/osh/host/`; `src/runtime/rx_caproot.c` | OSH gates its own spawn/redirection/chdir effects against capability grants and journals intent/outcome; **this slice closes all inherited descriptors above stderr before exec** | Child runs with full Linux-user authority; no rlimits, seccomp, Landlock, namespaces or cgroups; check and `execve` are two steps |
| AIENOS/Aienux kernel (QEMU only) | `aienos-apple-silicon-platform` @ `121a9dfb`, `native/kernel/core/osc_task.c` `ck_osc_launch` | Receipt-first launch of an admitted OSC unit; W^X mappings; per-launch capability table; tick budget; 16-page code/stack caps; 1-page input; up to 32 caller-owned workspace pages; `OUTCOME_UNKNOWN` on fault | Only admitted OSC units, not arbitrary `.om` ELF; QEMU/TEST, not physical hardware; one call/slot |
| Seeded `.om` host words (feature branch) | `seed/omc/omc.om`, `docs/one_layer/OMEGESE.md` section 10 | Array/type/range checks on arguments; no pointer or arbitrary-syscall escape | No launcher, no descriptor set, no capability check, no budgets; `execprog` preserves inherited descriptors and runs as the caller's uid |

The Linux patch is therefore an extension of the existing host owner. The kernel launcher is the model for admitted
units and should not be duplicated. The `.om` launcher remains a migration item for the #636/#649 owners, not a new
authority service created here.

## Ambient-input inventory at the OSH boundary

| Input | Current handling | Gap |
|---|---|---|
| Inherited descriptors | Known shell fds, pipes, redirection fds and 0-2 are closed; **now every other fd above 2 is closed before exec** | Explicit fd handoff has no syntax/contract; deliberately refused for now |
| Environment | OSH builds the child environment from its session table; `.om` `execprog` supplies an empty environment | Environment policy for arbitrary native children remains OSH's, not a capability |
| Working directory | Preserved; `chdir` is an OSH effect; `.om` has `cwd` read only | No namespace isolation |
| Filesystem namespace | Shared with the host user | No chroot/mount namespace/Landlock |
| IPC, sockets, pipes | Pipes created by OSH are known and closed; **inherited sockets were previously leaked and are now closed** | No network policy |
| Subprocesses | OSH forks/execs; no rlimits, cgroups or CPU/memory/wall-time budgets | Resource exhaustion is not contained |
| Device handles | Any inherited device fd above 2 is closed; opening a device is an OSH `open` effect | Child can still open devices the user can open |

## The defect and the fix

`src/osh/host/osh_exec.c` closed only descriptors OSH had created or knew about. An embedder-inherited descriptor
survived `fork`/`execve` and was usable by the child even when the policy granted no read capability.

Demonstrated before the change (`670-inherited-fd-before.log`):

- policy: `principal 1` and `allow spawn /bin/sh` only (no read grant);
- command: `osh --caps policy -c '/bin/sh -c "cat <&9"' 9</etc/hostname`;
- result: exit 0 and `spark-b87b`, the contents of the protected file;
- a second probe showed fd 9 and two embedder pty fds 16/18 in the child's `/proc/self/fd`.

The change adds `close_inherited_fds()` in the forked child, after the known descriptors are closed and before
`execve`/builtin dispatch. It uses `close_range(3, ~0U, 0)`; where the syscall is unavailable it falls back to a
bounded loop over `_SC_OPEN_MAX`, capped at 65536. Descriptors 0, 1 and 2 are the only ones preserved.

After the change (`670-inherited-fd-after.log`): fd 9 is absent from the child, `cat <&9` fails with
`Bad file descriptor`, and the child sees only 0, 1, 2 plus the `ls` handle for `/proc/self/fd`.

## Evidence and identities

| Item | Identity |
|---|---|
| `src/osh/host/osh_exec.c` before | sha256 `5001ceba594da08874436d5f4622326118d7453cb180d96c8029d04204acd735` |
| `build/osh/osh` before | sha256 `410bcec62d1ba1e6cb5c4ec6172e71eeabe158fd784a8bf0f7a7f352f02ad83e` |
| `src/osh/host/osh_exec.c` after | sha256 `ed9e2e68dcefcfc7f3e61bd0a6411125607e8cff00ef91c2b6631cded9877dc9` |
| `build/osh/osh` after | sha256 `ca224fa624c4208c4ba9bd4536ae6f3662fc85deec17d821350166081f434843` |
| Test source after | `tests/osh/host/test_osh_host.c` sha256 `515845ea1b636f43f5272b4ea266234a12b90b7e2bfb24e0880fc7315fadfe7a` |
| Platform | AArch64 Linux, native, kernel 7.0.0-1019-nvidia |

The tests were run with the harness's own descriptors 3..255 closed first, so an embedder fd is visible rather than
masked by the terminal that launched the test.

| Command | Result |
|---|---|
| `make test-osh-host` before source change, clean fd table | `checks passed 1568 failed 1`; the one failure is the new inherited-fd assertion (RED) |
| `make test-osh-host` after change | `checks passed 1569 failed 0`, `OSH_HOST_PASS` (plain and ASan/UBSan) |
| `make test-osh-caps` | 77 passed, 0 failed, `OSH_CAPS_PASS` (plain and ASan/UBSan) |
| `make test-osh-journal` | 185 checks, 0 failed, `OSH_JOURNAL_PASS` (plain and ASan/UBSan) |

## What this does not establish

- The child still runs with the full authority of the Linux user. There is no seccomp, Landlock, namespace, cgroup or
  rlimit containment. A spawned `/bin/sh` can open or connect to anything the user can.
- The effect check and `execve` are still two steps; a path swapped between them is not prevented.
- Descriptor closure is not descriptor authorization. There is no explicit contract that assigns a descriptor to a
  child with an object identity, rights, generation and budget. Numeric reuse after close is not modeled.
- Revocation during queued or in-flight effects has no fencing point here. That belongs to #649/#659.
- The seeded `.om` path has no launcher and no capability checks. Adding one would be a second authority path unless
  the existing OSH/rx_caproot owners are migrated, so it remains BLOCKED on #636/#649, not implemented in this slice.
- No durable store, crash recovery, independent final-source review or real hostile-child campaign is claimed.

## Next executable steps

1. Add an explicit child-descriptor assignment contract to the existing OSH owner: assigned fd, object identity,
   permitted operation, grant generation, lifetime and budget; refuse everything else. Test guessed/stale/reused
   handles and closed/reopened objects.
2. Bind the check and dispatch at one fence (or a documented generation/fencing protocol) before `execve`. Reuse
   `rx_capq`/`fab_dispatch` for provider selection and `rx_generation` plus `osh_journal` for durable operation
   identity; test revocation before and after submission, including queued work.
3. Add external resource budgets (rlimits/cgroup/seccomp/Landlock/namespaces as available) and fail closed when the
   host cannot provide them. Measure CPU, memory, fd and wall-time exhaustion with disposable payloads.
4. Reuse `ck_osc_launch` for admitted OSC units instead of adding a second kernel launcher; keep the Linux and
   AIENOS profiles distinct. Migrate the Linux launcher/effect path to seeded `.om` under the #636/#649 owners;
   until that path is qualified, OSH remains the trusted native edge. Do not create a parallel authority service.
5. Independent review of the final source and a clean-checkout replay. The AIENOS launcher is QEMU-qualified only;
   physical hardware remains NOT_RUN. Until these gates close, #670 stays open.
