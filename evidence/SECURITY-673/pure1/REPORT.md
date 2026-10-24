# Actual kernel-boundary qualification, author campaign

This campaign preceded the integrated `.om` gate in `../pure4`. Its working directory was
`/home/drakestapleton/workspace/omega-security673-qualified`. The unchanged production compiler
source is now committed as `265e19c0144cccf8a184c7b26b30c9e092074c8f` on the existing security
proposal. This report does not retroactively claim that publication constituted independent review.

The attacker controls a static AArch64 child and its machine instructions. The trusted launcher,
selected executable content, Linux kernel and caller-assigned descriptors0/1/2 remain assumptions.
The asset tested is access to files, inherited descriptors, networking, process creation and bounded
CPU/regular-file output. Synthetic `private.data` and `judge.data` are disposable fixtures. No private
input, real holdout, GPU, driver, deployment or automatic promotion was used.

Enforcement is existing `seed/omc/omc.om:hpack,hpure`: close_range, Landlock inode access,
no-new-privileges, seven prlimit64 ceilings, seccomp and execveat. The supported machine is Linux
AArch64 with those facilities. See `../pure4/platform.txt` for the exact executed kernel.
GNU strace6.8 observes actual kernel returns; GNU objdump2.42 independently decodes the raw images.
These external tools are qualification oracles, not first-party production fallbacks.

## Raw commands and outcomes

The recorded basename prefix below is `build/security673-pure/`. Every fixture was compiled from
its identically named `.om` source under `seed/qualification/security673/` with `stage2`, followed
by `readelf -h` and chmod only on build success. Compiler construction was:

```
seed/rung4/om4.bin < seed/omc/omc.om > build/security673-pure/stage1
build/security673-pure/stage1 < seed/omc/omc.om > build/security673-pure/stage2
cmp build/security673-pure/stage1 build/security673-pure/stage2
```

All three exited0. Each tested executable was format-inspected before execution. Commands used
`timeout 10s strace -f -o <case>.strace <launcher> <child>`, with stdout/stderr retained by case.
The positive child received `printf 'public-input'` on stdin. Cases and exact outcomes:

| Launcher | Child | Exit | Independent observation |
| --- | --- | --- | --- |
| pure-launch | pure-child | 0 | public-output; requested private/judge access, socket, clone and extra descriptors denied |
| pure-launch | raw-0 | 0 | absolute execveat of /usr/bin/true denied EACCES |
| pure-launch | raw-1 | 0 | raw openat denied EPERM |
| pure-launch | raw-2 | 0 | raw getpid denied EPERM |
| pure-launch | raw-3 | 0 | raw1GiB mmap denied EPERM |
| pure-launch | raw-other | 0 | absolute execveat of unassigned static pure-other denied EACCES |
| launch-mutant-4 | raw-2 | 71 | removed seccomp permits getpid, exact expected-denial assertion fails |
| launch-mutant-5 | raw-other | 77 | removed Landlock restriction permits unassigned static executable, whose only result is77 |
| pure-launch | pure-busy | 152 | planned SIGXCPU24, not an unexpected crash |
| pure-launch | pure-output | 0 | single1048577-byte write returns1048576; stat confirms exactly1048576 bytes |

`branch-mutate 4/5 < seed/omc/omc.om` generates the separately compiled bypass compilers. Their
`hsys277/446` calls are replaced exactly once by harmless getpid172; the absence of the intended
filter/fence causes the named forbidden effect. `pure-raw 0 <template>` uses /usr/bin/true;
`pure-raw 0 /home/drakestapleton/workspace/omega-security673-qualified/build/security673-pure/pure-other
<template >raw-other` uses the harmless static fixture. The latter avoids a dynamic-loader failure
masquerading as a caught inode bypass. The raw generator's original full512-byte input loophole was
corrected; final source requires exactly212bytes and EOF, with211/213/512 controls in `pure4`.

Missing-facility probes used this exact command pattern, once for each table row:

```
timeout 10s strace -f -e inject=<call>:error=<errno> -o unavailable-<call>.strace build/security673-pure/pure-launch build/security673-pure/pure-other
```

| call | errno | exit |
| --- | --- | --- |
| close_range | ENOSYS | 38 |
| landlock_create_ruleset | ENOSYS | 38 |
| seccomp | EACCES | 13 |
| prlimit64 | EPERM | 1 |

Each stderr is exactly `PURE_LAUNCH_REFUSED` followed by newline, stdout empty and trace has no
execveat. This is external syscall fault injection, not emulation or a claim about every kernel.
`sha256sum -c protected-before.sha256` returned0 for both synthetic protected files after the tests.

## Limits and corrections

Source/binary/fixture identities are in `identities.txt`; raw artifacts are archived alongside this
report. Hashes identify bytes; they do not prove code correct. The `.om` generator and checker still
share compiler lineage. This is bounded native testing, not formal proof or diverse compilation.

Descriptor3 closes on successful exec. Stdio remains caller-granted authority. Inode pinning does
not seal content, authenticate a grant, enforce revocation or create durable budgets. Setup errors
may leave restrictions installed; the supported adapter exits without fallback. CPU limits do not
bound blocked-I/O wall time, and file-size limits do not bound pipe output.

The inspected host core_pattern invokes Apport. Linux ignores RLIMIT_CORE for piped core handlers;
therefore setting CORE0 does not qualify no-core/private-memory confinement on this host. No
conclusion is drawn about dump persistence or upload. Private/holdout input use requires the existing
runtime owner to qualify a disposable environment's crash policy. This campaign used synthetic data.
