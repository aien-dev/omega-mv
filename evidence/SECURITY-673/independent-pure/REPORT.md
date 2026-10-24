# Independent review: execpure kernel profile and corrected supervisor

Date: 2026-10-23. Reviewer: `review_branch_qualification`, separate review agent.
Final author tree: `/home/drakestapleton/workspace/omega-security673-reviewed`, based on `8dfe7788bd599e844a4c00d8d0ae05447b921030`. The earlier qualified tree was preserved. Review snapshots are identified below; no author source was edited by this reviewer.

**Disposition: the bounded native kernel-profile checks pass. The planned-signal verdict defect is corrected and independently tested against a compiled bypass. Private-input and hidden-evaluator confinement remain BLOCKED by unqualified crash collection and the other authority/supervisor requirements below. This does not close #670, #607, RSI#67, or the whole security workstream.**

## Findings and corrections

1. **P2, corrected: a normal exit152 could masquerade as SIGXCPU.** The initial `reap` implementation mapped signal24 to152, while also returning normal exit152 unchanged; `purelim` accepted either. The final source explicitly refuses a normal wait result whenever a signal is required. Independent final `gate 5` returned99. A compiled exact-source mode6 mutation removing only that refusal returned0 on the same normal-exit152 child. The negative control therefore detects a real qualification failure.
2. **Earlier reviewer coverage claim corrected:** the older `control()` and main path unconditionally returned99 after a hypothetical accepted child. I missed this in the earlier review. Those historical99 observations alone did not qualify the negative-control verdict logic. The author removed the unconditional failure. Independent final controls0,1,2 each returned99 through the actual wrong-exit, signal, and deadline refusals; an erroneous acceptance now reaches exit0. Earlier files remain historical evidence, not overwritten passes.
3. **Open private-input qualification blocker: host crash collection.** The busy test received the planned SIGXCPU, and the wait status included its core flag. Read-only inspection found `core_pattern` pipes to Apport, `core_pipe_limit=10`, and `suid_dumpable=2`. Linux explicitly does not enforce RLIMIT_CORE for piped core handlers. Thus `RLIMIT_CORE=0` cannot establish absence of core material on this host. No persistence, upload or private-data leak was demonstrated, and no unrelated crash records were inspected. The exact host configuration is retained in the reviewer evidence. [Linux core-dump rules](https://man7.org/linux/man-pages/man5/core.5.html).
4. **Documentation corrections:** descriptor3 is temporary and CLOEXEC closes it on successful execution; stdio remains caller-assigned authority. Inode pinning is not immutable content identity or grant-generation binding. RLIMIT_NPROC alone is not the fork boundary. In the additive branch fixtures, comparison with zero uses CMP plus B.NE; the divide-by-zero fixture supplies the observed CBZ. The author corrected these claims.

The private-input blocker belongs to the existing launcher/evaluation/platform owners. Next executable step: qualify a disposable execution environment whose crash-collection policy is explicitly controlled, using synthetic crash markers and independently observing the collector/storage boundary. Do not change this host's global crash policy or use private/holdout data as a substitute.

## Threat model, enforcing code and platform

The tested adversary is an executed native candidate attempting unassigned pathname access, process creation, raw system calls, or execution of a different inode after the trusted launcher installs restrictions. Protected assets in these tests are disposable private/judge files, assigned stream boundaries and the selected executable inode. The tests also exercise CPU and regular-file output ceilings.

The existing compiler owner implements the new `execpure` word through `hpack` and `hpure` in `seed/omc/omc.om`, source hash `2e6e1768...`. Enforcement observed in real kernel calls is:

- Close descriptors3 and above in a private descriptor table; open the selected executable O_PATH|O_CLOEXEC.
- Install a Landlock ruleset handling ABI3 filesystem rights, granting read/execute only to that selected inode, with no-new-privileges.
- Set per-process CPU, regular-file size, stack, core, process-count, descriptor-count and address-space limits.
- Install an architecture-checking seccomp filter permitting read0, write1/2, exit/exit_group and constrained execveat. Other calls return EPERM. The Landlock inode fence is necessary because an absolute execveat pathname ignores its dirfd.
- Invoke the selected executable with execveat and AT_EMPTY_PATH. A setup error branches around execution and returns its negative kernel result; no weaker fallback is emitted.

The filter layout, syscall numbers, argument offsets and branches were inspected. Real syscall observations and independently decoded raw fixtures supply evidence beyond helper predicates. Seccomp cannot inspect pointed-to pathname bytes, so the inode fence is a separate enforcing boundary. [Linux seccomp documentation](https://docs.kernel.org/userspace-api/seccomp_filter.html), [execveat semantics](https://man7.org/linux/man-pages/man2/execveat.2.html), [Landlock documentation](https://docs.kernel.org/userspace-api/landlock.html).

Supported platform tested: Linux7.0.0-1019-nvidia on AArch64 GNU/Linux, strace6.8, GNU objdump2.42. These are native CPU/kernel tests, not device or GPU qualification, emulation, formal proof, or diverse compilation. The test programs made no network connections; attempted socket creation in the fixed hostile consumer was denied before creating a socket. The host crash collector's subsequent activity was not qualified.

## Independent kernel-boundary results

Manual snapshot and evidence: `/tmp/omega-pure-review.VkogeG/`.

| Case | Actual result |
| --- | --- |
| Hostile `.om` consumer with public stdin | Exit0; public output correct; attempts to read/write wrong streams, open private/judge paths, create socket/process, and re-exec refused |
| Raw absolute execveat to unassigned executable | Fixture exit0 after EACCES |
| Raw openat | Fixture exit0 after EPERM |
| Raw getpid | Fixture exit0 after EPERM |
| Raw1GiB mmap | Fixture exit0 after EPERM |
| Raw absolute exec to harmless static `.om` other fixture | Fixed launcher exit0 after refusal |
| Compiled seccomp-install bypass | Raw getpid exposed; fixture exit71 |
| Compiled Landlock-application bypass | Actually executed the unassigned static fixture; exit77 |
| CPU-busy program through fixed profile | Planned SIGXCPU; shell status152; trace identifies SI_KERNEL and signal24 |
| Same busy binary without profile | External two-second timeout124; did not naturally produce the planned signal/result |
| Output program through fixed profile | Exit0; one1048577-byte write returned1048576; file exactly1048576 bytes |
| Same output binary without profile | Exit82; file1048577 bytes, exposing absent limit |
| Raw mmap with filter bypass but profile limits retained | Exit71; kernel returned ENOMEM for1GiB under the128MiB address-space limit |
| Injected missing Landlock create | Launcher38; no execveat |
| Injected denied Landlock rule | Launcher13; no execveat |
| Injected denied Landlock application | Launcher1; no execveat |
| Injected missing seccomp | Launcher38; no execveat |
| Injected denied prlimit64 | Launcher1; no execveat |

The two synthetic protected files and all initially identified input/binary artifacts still matched their pre-run hashes at the final identity check, exit0. This is observed byte preservation for this run, not proof of immutable artifact storage against concurrent outside writers.

The raw fixtures are generated as fixed native images by `.om`; they are not a foreign production compiler or a second general source translator. Their ELF headers and instruction streams were independently inspected using readelf and objdump before execution. The inode-bypass control targets a harmless reviewer-owned static `.om` program returning77, not a system service.

Fault injection occurred on this kernel through strace. It is not physical testing of an older kernel or actual kernel-memory exhaustion. The planned SIGXCPU is an intentional policy event, not an unexpected crash reclassified as a pass. The core flag is logged and its confidentiality implication remains open.

## Final integrated qualification and additive tests

Final runner snapshot and evidence: `/tmp/omega-pure-gate-review.DGVH2e/`.

| Final command | Exit |
| --- | --- |
| `./gate 4` strict profile | 0 |
| `./gate` branch regression, including additive forms/packets | 0 |
| `./gate 3` eighteen-case source campaign | 0 |
| `./gate 5` normal152 presented where SIGXCPU is required | 99 |
| `./gate-spoof-mutant 5` compiled removal of that guard | 0, detected weakened supervisor |
| `./gate 0` wrong normal exit | 99 |
| `./gate 1` unexpected signal | 99 |
| `./gate 2` deadline overrun | 99 |

Mode4 independently rebuilds the actual compiler twice, checks the hostile consumer's output and protected bytes, exercises raw fixtures and compiled bypasses, checks planned SIGXCPU, and counts exactly1MiB output bytes. It also refuses raw-generator inputs of211,213 and512 bytes with92 and empty output.

All fifteen additive branch-form programs returned the predeclared outcomes. Independent decoding of zero-comparison mode6 showed CMP+B.NE; mode14 showed the division guard's CBZ. These are bounded target-selection checks, not coverage of every branch span or full semantics proof.

All nine additive packet cases were independently replayed under strace. Valid modes0/8 executed the external `/bin/true` fixture and returned0. Seven malformed cases exited103 before close_range or target execve. This is bounded packet testing, not comprehensive packet fuzzing. `/bin/true` is an explicitly identified external test consumer, not an Omega production fallback.

## Raw commands, logs and preservation

Both snapshot directories must be preserved in durable evidence before temporary files are reclaimed. Every integrated child command, redirect, actual/expected exit and measured interval is recorded in `evidence/pure.log`, `normal.log`, `hostile.log`, `spoof-*.log` and `control*.log`. Corresponding `.exit` and `.err` files distinguish results from missing runs.

Manual reconstruction began with existing om4, then the resulting stage1 rebuilt the identical stage2. Fixtures and both compiler mutants were compiled with stage2. Every build and readelf inspection exited0 before executable permission was added. The precise source/binary mappings are in `evidence/identities.txt`.

Representative raw manual commands, working directory `/tmp/omega-pure-review.VkogeG`:

```sh
seed/rung4/om4.bin < seed/omc/omc.om > build/security673-pure/stage1 2> evidence/stage1.err
readelf -h build/security673-pure/stage1 > evidence/stage1.header
chmod +x build/security673-pure/stage1
build/security673-pure/stage1 < seed/omc/omc.om > build/security673-pure/stage2 2> evidence/stage2.err
readelf -h build/security673-pure/stage2 > evidence/stage2.header
chmod +x build/security673-pure/stage2
cmp build/security673-pure/stage1 build/security673-pure/stage2
# each exit0
build/security673-pure/branch-mutate 4 < seed/omc/omc.om > build/security673-pure/filter-bypass.om
build/security673-pure/branch-mutate 5 < seed/omc/omc.om > build/security673-pure/inode-bypass.om
# each exit0; both transformed compilers and launcher consumers were built and ELF-inspected successfully
timeout 10s strace -f -qq -o evidence/filter-bypass.strace -e trace=execve,landlock_restrict_self,seccomp,execveat,getpid build/security673-pure/filter-launch build/security673-pure/raw-2 < /dev/null > evidence/filter-bypass.out 2> evidence/filter-bypass.err
# exit71
timeout 10s strace -f -qq -o evidence/inode-bypass.strace -e trace=execve,landlock_restrict_self,seccomp,execveat,getpid build/security673-pure/inode-launch build/security673-pure/raw-other < /dev/null > evidence/inode-bypass.out 2> evidence/inode-bypass.err
# exit77
/usr/bin/time -v -o evidence/busy.resources timeout 10s strace -f -qq -o evidence/busy.strace -e trace=execve,prlimit64,execveat build/security673-pure/pure-launch build/security673-pure/pure-busy < /dev/null > evidence/busy.out 2> evidence/busy.err
# planned signal24, shell152
timeout 2s build/security673-pure/pure-busy < /dev/null > evidence/busy-unrestricted.out 2> evidence/busy-unrestricted.err
# exit124
timeout 10s strace -f -qq -o evidence/output.strace -e trace=prlimit64,execveat,write build/security673-pure/pure-launch build/security673-pure/pure-output < /dev/null > evidence/output.bin 2> evidence/output.err
# exit0
timeout 10s build/security673-pure/pure-output < /dev/null > evidence/output-unrestricted.bin 2> evidence/output-unrestricted.err
# exit82
wc -c evidence/output.bin evidence/output-unrestricted.bin
# exit0:1048576 and1048577 bytes
sha256sum -c evidence/identities.txt > evidence/final-identity-check.log
# exit0
cat /proc/sys/kernel/core_pattern > evidence/core-pattern.txt
cat /proc/sys/kernel/core_pipe_limit > evidence/core-pipe-limit.txt
cat /proc/sys/fs/suid_dumpable > evidence/suid-dumpable.txt
# each exit0; read-only host policy inspection
```

The five failure-injection commands use `timeout 10s strace -f -qq`, the fixed `pure-launch` and `pure-child`, `/dev/null` input, and these exact trace/injection pairs; each output/error file is separately retained:

| Trace file | strace options | Exit |
| --- | --- | --- |
| `missing-landlock.strace` | `-e trace=landlock_create_ruleset,execveat -e inject=landlock_create_ruleset:error=ENOSYS` | 38 |
| `denied-landlock-rule.strace` | `-e trace=landlock_add_rule,execveat -e inject=landlock_add_rule:error=EACCES` | 13 |
| `denied-landlock-apply.strace` | `-e trace=landlock_restrict_self,execveat -e inject=landlock_restrict_self:error=EPERM` | 1 |
| `missing-filter.strace` | `-e trace=seccomp,execveat -e inject=seccomp:error=ENOSYS` | 38 |
| `denied-limit.strace` | `-e trace=prlimit64,execveat -e inject=prlimit64:error=EPERM` | 1 |

Final integrated commands, working directory `/tmp/omega-pure-gate-review.DGVH2e`, preserving `build/security673-native` under a distinct suffixed directory after each run:

```sh
./mutator 6 < seed/qualification/security673/gate669.om > gate-spoof-mutant.om
/tmp/omega-pure-review.VkogeG/build/security673-pure/stage2 < gate-spoof-mutant.om > gate-spoof-mutant 2> evidence/spoof-mutant-build.err
readelf -h gate-spoof-mutant > evidence/spoof-mutant.header
chmod +x gate-spoof-mutant
# each exit0
/usr/bin/time -v -o evidence/pure.resources ./gate 4 > evidence/pure.log 2> evidence/pure.err
# exit0
/usr/bin/time -v -o evidence/normal.resources ./gate > evidence/normal.log 2> evidence/normal.err
# exit0
./gate 3 > evidence/hostile.log 2> evidence/hostile.err
# exit0
./gate 5 > evidence/spoof-fixed.log 2> evidence/spoof-fixed.err
# exit99
./gate-spoof-mutant 5 > evidence/spoof-mutant.log 2> evidence/spoof-mutant.err
# exit0, weakened verdict exposed
./gate 0 > evidence/control0.log 2> evidence/control0.err
./gate 1 > evidence/control1.log 2> evidence/control1.err
./gate 2 > evidence/control2.log 2> evidence/control2.err
# each exit99; directories preserved between commands
```

The integrated strict run took1.13 seconds wall time and1.01 user seconds in GNU Time's observation. The manually traced CPU run accumulated0.99 user seconds before SIGXCPU. These are run observations, not timing guarantees or aggregate resource proofs. No unexpected crash, missing execution or failed build was accepted in the final reviewer runs.

## Exact identities (SHA256)

Complete fixture/output identities are retained in the snapshots' manifests. Important linkage:

| Artifact | SHA256 |
| --- | --- |
| Compiler source | `2e6e17680cfab319f28842fa68c9e6520e5412c4831f80ab109945087bdd0ad3` |
| Existing om4 | `d0da40e45f1fcc45d9a39c9c52c9d745b545e076970729e51c12d76ef519efab` |
| Independently rebuilt stages1/2, manual and integrated | `87542cfdf91e9666851b6e21d16c3a281547a8f7520dcedcd1f2a8e8540285e1` |
| Final gate source | `9b1948fa29c4b5d91517c3998511acea9ab1d09658d81adc6fc0e135880703c9` |
| Final mutator source, modes0..6 | `2c0328d114ff622af869c5a68bcf013292a9f5d938c507df1db98fd1c16f234d` |
| Final gate-child source | `38d346809a567d91c494a2c35f7a91ab87cb70603478e343427d710408b32de1` |
| Final gate binary | `5f6d8b75b59404c2947d390ed95c34fc8aa2c8b6a0f1bd1df7652571ecd76fef` |
| Spoof-mutant gate source | `bdacd9fbdaf0def6e97df7c31231d0c6574a552e32c7852f01d48fe52c79b367` |
| Spoof-mutant gate binary | `0dceb30eccf628e84ecc9ab521eb74e2d46141c5892636b9acbf854eed4ff728` |
| `pure-launch.om` | `9061033def1f3f43402b51a396effc96154160de55547ac35b686af9ac2278b0` |
| `pure-child.om` | `95c63d2aa7d69e6e04752279885496060a4152b5c1604ff75d65b201d48c713b` |
| `pure-raw.om` | `58b9a1376577458af3eefeee53e2560ef1ba28f2aa80f1c1e31aea0891fb9e42` |
| `pure-busy.om` | `d4801acf04a44467bac5e1bedfc502b20afe009d8065079ae5880e987b0899a7` |
| `pure-output.om` | `dfd33a5f59acf7b5c411c927f3a100b0661cd6ee0c810f8aea41f1aa76188cd8` |
| `pure-other.om` | `a3f0c305294d59d459fe3428dcfcb192354322fe06fbba4e9bbc403d42efa6c2` |
| Manual launcher binary | `782e81247c1dfa07b19ac1987a98152e0063d2fa7e4078c1c6f54b596a7bfa32` |
| Manual hostile-child binary | `d5886f82fa961ac7110b3f4fd2d4ce7073458136dffe215c11d0f0a8d531c854` |
| Manual seccomp-bypass launcher | `18bd6a5eacf8ba965058b25bb589966a0433ff000b24303e9ee3dc6e1e0f70aa` |
| Manual inode-bypass launcher | `522849a3af26d104bdce5c1ba09e094a4e0b8df80cf0168ff69ab9b9a4159dc2` |
| Strict-run artifact manifest | `99e458819fa633782d66124ac77318cb49c4940941dc89ac05138de9ea048c6d` |
| Normal-run artifact manifest | `44b87d556f18c94e34784187f320823be5b70cd91cb39f0f90c827df7ca02079` |
| Hostile-source artifact manifest | `bfb90c3d7bbeba1f7faf2d6e9f9901b139fd05ac986076b608865f96b2aa6155` |
| Manual `final-evidence.sha256`, including traces and crash-policy files | `4a81f2abf6b152a9ced37e0f2d74d9975c08749cd65111b3814a904af286288c` |
| Integrated `final-evidence.sha256`, including exits and verdict logs | `966a6b6de01b9cf30962b61e4217abf790711c8dae203a773e9e4cb857147a49` |
| `/bin/true`, external packet consumer | `a82c1b5fd392d72148d53992a860224ff3753cb2dc8909e9bdd1620e54ee1b88` |
| strace | `33b3ea325915f1213dc477d80e0f0b2b09ffeff5e81d98b25a35320fbcbcaf42` |
| objdump | `f2e917a5d118ebf5679b24740e20d30338308e7f3f0630527c469de452d5a250` |

The manual snapshot contains the earlier modes0..5 mutator source, identified separately in its `identities.txt`; the final integrated snapshot contains modes0..6. The compiler source is identical in both. Matching hashes and fixed-point binaries establish identity/reproducibility, not correctness.

## Residual assumptions and unqualified requirements

- **Private or hidden evaluator input is not qualified on this host.** The crash-handler boundary must be resolved in the existing execution owner before such a claim. RLIMIT_CORE0 and a matching hash are insufficient.
- The selected inode's bytes must be protected by the trusted supervisor against outside writers. Pinning prevents pathname replacement from selecting a different inode during the initial exec; it does not freeze content. The filter also does not constitute a one-shot execution capability.
- All three standard descriptors must be present and intentionally assigned. They remain real capabilities, potentially including authority beyond ordinary pipes/files if misassigned. Pipe-output accounting, bounded input and wall supervision remain external requirements. Regular-file limits are per file, not an aggregate output budget.
- This supports static Linux AArch64 executables on the tested kernel. Dynamic loaders, interpreters, older kernels, shared-file-table races and architecture-mismatch behavior were not broadly qualified. Missing-feature behavior was fault-injected, not demonstrated across physical older systems.
- Process denial comes from seccomp. The test does not prove all rlimit properties or deny every possible side channel. No independent formal proof of the complete BPF policy or compiler translation was performed.
- Grant identity, revocation, generation fences, durable recovery/budgets, protected evaluation/promotion stores and authority-aware reuse remain outside this primitive. No competing evaluator or authority service was added by the reviewer.
- Test fixtures and verdict logic share the seed compiler lineage. Kernel observations, objdump and explicit compiled bypasses provide separate checks, but not diverse compilation or universal correctness.
- All workload data was synthetic and disposable. No host configuration was changed, no unrelated core reports were read, and no driver, GPU/model work, private upload, deployment or automatic promotion was performed. No live service was reconfigured; the configured crash-collection path is the explicit unresolved boundary above.

This report is additive evidence and a correction of earlier reviewer scope. It does not rewrite historical failed attempts or authorize release/issue closure.
