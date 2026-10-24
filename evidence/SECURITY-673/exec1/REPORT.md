# Native descriptor boundary prerequisite for #670

Status: bounded Linux AArch64 descriptor handling QUALIFIED; full #670 remains OPEN.

Threat: a child executable unintentionally inherits an already-open file/socket/device capability
from its trusted caller. Protected asset: resources accessible through extra inherited descriptors.
The enforcing code is `seed/omc/omc.om:hpack` in the existing `execprog` lowering. After complete
packet validation and before `execve`, generated code invokes Linux syscall436,
`close_range(3, UINT_MAX, CLOSE_RANGE_UNSHARE)`. This preserves only caller-assigned standard
streams and obtains a private descriptor table. On any nonzero result, `execve` is skipped and
the kernel error is returned. There is no finite-descriptor-loop or permissive fallback.
The Linux API contract is documented at https://man7.org/linux/man-pages/man2/close_range.2.html .
Local `asm-generic/unistd.h` and `linux/close_range.h` independently identify syscall436 and flag2.

Actual integration tests are seeded `exec-parent.om` and `exec-child.om`. Each test creates a
disposable synthetic-secret file, holds it at descriptors3 and65537 without close-on-exec, creates
pipes for public input/output, forks and executes the real compiled child. The child must receive
EBADF for both extra handles and exchange the exact public payload through its assigned streams.
The parent rereads its own synthetic file after the child exits, proving its descriptor table was
preserved. Missing executable mode checks closure even when `execve` returns ENOENT. A high-only
control closes descriptor3 before exec to isolate65537. No private data or device handles are used.

Author results in `exits.txt`: green0/1/2=0; old-compiler red0=42, red1=55, red2=43. These are
specific failed assertions about low-handle inheritance, error-path inheritance and high-handle
inheritance. They are ordinary exits, not crashes. `unavailable3=0` is a separately declared
test of a compiled source mutant replacing syscall436 with unsupported999; it requires ENOSYS
and no child execution. `injected3=0` separately uses external strace fault injection on the real
close_range call. Neither is physical old-kernel qualification. `traced0/1=0` confirm successful
and missing-path exec ordering with real Linux calls. Every trace contains synthetic data only.

The independent checker is the external kernel/strace observation plus a separately run review
of exact sources and real child behavior. Independent review/raw artifacts are retained under
`independent670`. It reproduced all three positive cases, all old-compiler failures and the
unavailable-syscall control. All source, compiler, binary and tool hashes are in `identities.txt`
and the independent report. Stage1 and stage2 equal6b5d9403; this is same-lineage reproducibility.

Existing `sh seed/host/check.sh` exited0 with its retained host suite,41 negative controls and70
original fixture comparisons. `host-check.*` records the actual run. The complete native669 gate
also exited0 on this compiler; `gate669.*` and its artifact archive retain the raw result. Existing
#659 modules were replayed unchanged natively: action219, delegation74, reuse74 assertions pass.
The existing L775 benchmark core also rebuilt and passed185 checks. Those separate component
replays qualify compatibility only, not durable dispatch, authenticated policy or evaluator custody.

Raw author build sequence:

```text
seed/rung4/om4.bin < seed/omc/omc.om > build/security673-exec/stage1
readelf -h build/security673-exec/stage1
chmod +x build/security673-exec/stage1
build/security673-exec/stage1 < seed/omc/omc.om > build/security673-exec/stage2
cmp build/security673-exec/stage1 build/security673-exec/stage2
chmod +x build/security673-exec/stage2
build/security673-exec/stage2 < seed/qualification/security673/exec-parent.om > build/security673-exec/exec-parent
build/security673-exec/stage2 < seed/qualification/security673/exec-child.om > build/security673-exec/exec-child
```

Each exited0; both test executables were inspected with readelf and made executable only after
successful builds. Build stderr is retained. Each fresh test directory contains a copy of the child:

```text
cd build/security673-exec/traced0
printf '\000' | timeout 10s strace -f -o ../../../evidence/SECURITY-673/exec1/traced0.strace -e trace=close_range,execve,read,write ../exec-parent
exit0
```

`traced1` uses byte1 and checks missing executable. `green2` uses byte2. `red0/1` use the initial
parent compiled by7c25129a; `red2` uses the final parent compiled by that same old compiler.
The exact independent commands for the final sources and every result are in `independent670/REPORT.md`.
The author's initial identity-glob command encountered a directory and exited1; it was an archival
command error, not a passing test. The corrected explicit-file identity command produced the manifest.

Residual scope: kernel, filesystem and trusted caller are assumed. Stdio must be intentionally
assigned. Higher-descriptor inheritance is unsupported; failure after successful closure leaves
extra handles closed. Ambient working directory, filesystem paths, networking, subprocess creation,
CPU/memory/output limits, grant generations and revocation fencing are not confined by this change.
Arbitrary native code still has ordinary user authority. The existing trusted launcher and capability/
durable-effect owners must enforce those remaining requirements. No complete sandbox or #670 closure
is claimed. The original DeepSeek C candidate remains preserved on its original branch; this proposal
contains only seeded first-party implementation and tests and does not qualify that C consumer.
