# Independent review: #670 execprog descriptor prerequisite

Date: 2026-10-23. Reviewer: `review_branch_qualification`, separate review agent.
Author tree: `/home/drakestapleton/workspace/omega-security673-qualified`.
Base commit: `7a1692908d86c69be91191ac227ce465d4ee8b1d`; this host change was uncommitted when inspected.

**Verdict: no correctness finding in the reviewed descriptor-inheritance prerequisite. Independent native integration tests reproduced the positive and negative controls. This does not satisfy the full least-authority or effect-boundary authorization requirements of #670.**

## Threat, asset, property and enforcement

A process launched through the seeded compiler's `execprog` should not accidentally receive the caller's open handles beyond the explicitly assigned standard streams. The protected asset is access through those inherited handles, including high-numbered descriptors. This change does not prevent reopening a resource through ambient filesystem authority.

In `seed/omc/omc.om`, `hpack`'s `c == 124` path, starting at line2457, now emits `close_range(3, UINT_MAX, CLOSE_RANGE_UNSHARE)` before `execve`. It branches past `execve` on every nonzero result and returns the kernel error. No guessed descriptor ceiling or permissive fallback is emitted. Existing packet validation precedes the close. The rename path is unchanged. This extends the existing host lowering; it introduces no second authority implementation.

Local AArch64 syscall headers confirm numbers436 and221, and `CLOSE_RANGE_UNSHARE == 2`. The Linux manual describes inclusive range closure and unsharing to avoid descriptor-table races, and explicitly documents this pre-exec pattern. Its semantics support this narrow design; they do not establish application isolation. [Linux close_range manual](https://man7.org/linux/man-pages/man2/close_range.2.html).

Tested platform: Linux7.0.0-1019-nvidia, AArch64 GNU/Linux. Soft and hard open-descriptor limits were both500000. Oracle: strace6.8 observing real kernel calls, supplemented by the executed child's reads and the parent's public-output/retained-secret assertions. The successful mode was also rerun without strace.

## Independently reproduced results

All reviewer artifacts are in `/tmp/omega670-review.bScRBA/`. Preserve this directory with the durable evidence before temporary files are reclaimed.

| Case | Actual exit | Observed property |
| --- | --- | --- |
| New compiler, mode0 | 0 | Child reads on descriptors3 and65537 return EBADF; assigned stdin/stdout work; parent retains its own secret handle |
| New compiler, mode1 | 0 | Nonexistent executable returns ENOENT after closure; both extra descriptors remain closed in that child; parent retains its handle |
| New compiler, mode2 | 0 | High-descriptor isolation succeeds when descriptor3 was already closed before exec |
| Old compiler, mode0 | 42 | Child can read synthetic data through inherited descriptor3 |
| Old compiler, mode1 | 55 | Failed exec leaves inherited descriptor3 readable in the old implementation |
| Old compiler, mode2 | 43 | With descriptor3 absent, child can read synthetic data through inherited descriptor65537 |
| Unavailable-syscall mutant, mode3 | 0 | Injected syscall999 returns ENOSYS; child checks the negative result; trace contains no subsequent child execve; no public child output appears |
| New compiler, mode0 without tracer | 0 | Positive integration result reproduced without tracing |

The old compiler controls are meaningful failures of the tested property, not security-check passes. The unavailable-syscall run is controlled fault injection on the current kernel, not qualification on an actual older kernel.

Representative independent trace observations:

```text
close_range(3, 4294967295, CLOSE_RANGE_UNSHARE) = 0
execve("./exec-child", ["./exec-child"], ... /* 0 vars */) = 0
read(3, ..., 1) = -1 EBADF
read(65537, ..., 1) = -1 EBADF
read(0, "public-input", 12) = 12
write(1, "public-output", 13) = 13
```

`green1.strace` independently records successful closure followed by failed `execve` and EBADF for both handles. `unavailable3.strace` records ENOSYS from syscall999 with no child exec. Trace files contain only this disposable synthetic workload; no private-resource reads were used.

## Raw build commands and exits

The final source, fixtures and documentation were copied into `src/`; existing om4 into `bin/om4`; the independently rebuilt pre-change compiler from the earlier #669 review into `bin/old-cc`. Working directory for these commands was the reviewer artifact directory.

```sh
bin/om4 < src/omc.om > bin/stage1 2> stage1.err
readelf -h bin/stage1 > /dev/null
chmod +x bin/stage1
bin/stage1 < src/omc.om > bin/stage2 2> stage2.err
readelf -h bin/stage2 > /dev/null
chmod +x bin/stage2
cmp bin/stage1 bin/stage2
bin/stage2 < src/exec-parent.om > bin/exec-parent 2> parent-build.err
readelf -h bin/exec-parent > /dev/null
chmod +x bin/exec-parent
bin/stage2 < src/exec-child.om > bin/exec-child 2> child-build.err
readelf -h bin/exec-child > /dev/null
chmod +x bin/exec-child
bin/old-cc < src/exec-parent.om > bin/old-parent 2> old-parent-build.err
readelf -h bin/old-parent > /dev/null
chmod +x bin/old-parent
bin/stage2 < src/branch-mutate.om > bin/mutator 2> mutator-build.err
readelf -h bin/mutator > /dev/null
chmod +x bin/mutator
bin/mutator 2 < src/omc.om > src/unavailable.om
bin/stage2 < src/unavailable.om > bin/unavailable-cc 2> unavailable-cc-build.err
readelf -h bin/unavailable-cc > /dev/null
chmod +x bin/unavailable-cc
bin/unavailable-cc < src/exec-parent.om > bin/unavailable-parent 2> unavailable-parent-build.err
readelf -h bin/unavailable-parent > /dev/null
chmod +x bin/unavailable-parent
```

Every command above exited0 under `set -e`. No failed or empty compiler output was executed. Compiler fixed-point equality is recorded only as same-lineage reproducibility.

## Raw integration commands and exits

Each named case had a fresh directory and a copy of the identified `bin/exec-child`. The secret file was created exclusively by the `.om` parent inside that directory.

```sh
cd cases/green0
printf '\000' | timeout 10s strace -f -qq -e trace=close_range,execve,read,write -o ../../green0.strace ../../bin/exec-parent > ../../green0.out 2> ../../green0.err
# exit 0
cd ../green1
printf '\001' | timeout 10s strace -f -qq -e trace=close_range,execve,read,write -o ../../green1.strace ../../bin/exec-parent > ../../green1.out 2> ../../green1.err
# exit 0
cd ../green2
printf '\002' | timeout 10s strace -f -qq -e trace=close_range,execve,read,write -o ../../green2.strace ../../bin/exec-parent > ../../green2.out 2> ../../green2.err
# exit 0
cd ../red0
printf '\000' | timeout 10s strace -f -qq -e trace=close_range,execve,read,write -o ../../red0.strace ../../bin/old-parent > ../../red0.out 2> ../../red0.err
# exit 42
cd ../red1
printf '\001' | timeout 10s strace -f -qq -e trace=close_range,execve,read,write -o ../../red1.strace ../../bin/old-parent > ../../red1.out 2> ../../red1.err
# exit 55
cd ../red2
printf '\002' | timeout 10s strace -f -qq -e trace=close_range,execve,read,write -o ../../red2.strace ../../bin/old-parent > ../../red2.out 2> ../../red2.err
# exit 43
cd ../unavailable3
printf '\003' | timeout 10s strace -f -qq -e trace=all -o ../../unavailable3.strace ../../bin/unavailable-parent > ../../unavailable3.out 2> ../../unavailable3.err
# exit 0
cd ../plain0
printf '\000' | timeout 10s ../../bin/exec-parent > ../../plain0.out 2> ../../plain0.err
# exit 0
```

## Identities (SHA256)

| Artifact | SHA256 |
| --- | --- |
| Compiler source | `33ba46ab1fce0e2cab8f027187f55361bb9a949d597029454ca6641a756f65ac` |
| `exec-parent.om` | `b784dde3b862bfc7784015377447b3fffd4c6f1a37d04680e03960fbabc5a0de` |
| `exec-child.om` | `f2519f6be7068cf8af826101febd0f490327916322ac92a648dba9f3380324ec` |
| `branch-mutate.om` | `92b505c0328072322562efbc8c340671e6cd73ffc2356246d2fa110a992f5b38` |
| `OMEGESE.md` | `a85349d20ef20a4938ae8d4eebe1651b00fe5b9d6a110da83f3c386210514047` |
| Unavailable compiler source | `8065c74a1342c699d6a0a0c241461a5f3301c3486462f592958e3665d4b1b057` |
| Existing om4 | `d0da40e45f1fcc45d9a39c9c52c9d745b545e076970729e51c12d76ef519efab` |
| Compiler stages1 and2 | `6b5d94033b9705d78c8e85e806612aac5a94d90f217c54a97fe1891a2ce5aa55` |
| Old compiler, negative-control builder | `7c25129a0a8ec0604f93ec51eb2028ecfc6e04dde936a73aeaa5f24a2b3214b8` |
| New parent binary | `6723bdf57467e16a44984ce8d26fe784a4deac0c6f2e209bd96c822ff532025e` |
| Child binary | `4081e47ac39b4f1dff5f4367fad2f6ee0e45fbd47372f03559b9c559ed0e8014` |
| Old parent binary | `03d45d75ae66cfc15424675c78b318e4cdbd0a66d4c6d70c528bcee9263a2a6e` |
| Mutator binary | `41d00de1933c894a14b2125c727c33167ea4b6569ab24d9fa72cd94986a69daa` |
| Unavailable compiler binary | `2cbcd769891c3d3365cca73d22d871ae415dc75337116e36b1d8a9a7a15c8b12` |
| Unavailable parent binary | `74112ccaf838a53c480d8cc8b0e0c9c20bdce00e1077451e9ea8536b4e98f84f` |
| strace | `33b3ea325915f1213dc477d80e0f0b2b09ffeff5e81d98b25a35320fbcbcaf42` |
| timeout | `e9019c0a2fc634096cc1237f68b015fa8eb72f80cfc7d3fe69a1a4e2d9a24980` |

`identities.txt` contains source and tool identities. Hashes identify tested artifacts; matching hashes do not prove correctness.

## Compatibility risks and residual requirements

- This intentionally removes inheritance of descriptors >=3 from `execprog`. Consumers requiring such inheritance must be changed or explicitly treated as unsupported. A failed executable-path lookup still leaves the calling process's extra descriptors closed after a successful close_range. The updated documentation states both behaviors. Callers wishing to preserve their own handles should use the existing forked-child launch pattern.
- Actual older kernels, kernel-memory exhaustion, policy-denied close_range, and shared-file-table concurrency were not physically exercised. The code's nonzero-result refusal was inspected; ENOSYS was tested through a compiler fault mutant. UNSHARE behavior relies on the supported kernel interface.
- Stdio can itself carry sensitive authority if the caller assigns it. Working directory, pathname access, network creation, subprocesses, and arbitrary native code remain unrestricted. An executable can reopen the synthetic file by path. This slice protects the inherited-handle channel only.
- The host tests inspected in the author's `host-check.log` report exit0, 41 negative controls and 70 unchanged original fixtures. That is author evidence inspected here, not an independent full consumer audit. Root retains the full branch-gate/host-suite rerun and final identity linkage.
- No runtime permission check, durable authority store, revocation fence, evaluator/promotion protection, resource quota, or device isolation was added or qualified by this prerequisite. #670 and the rest of #673 remain open for those requirements.
- The compiler and tests share a seed lineage. Kernel tracing is an independent observation boundary; this is bounded native integration testing, not formal proof or diverse-double-compilation.
- Only disposable synthetic data and local resources were used. No deployment, live-driver/GPU change, private upload, or automatic promotion occurred. No tracked implementation or test source was modified by the reviewer.
