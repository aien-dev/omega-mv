# Independent review: public candidate boundary at L775

Date: 2026-10-23. Reviewer: `review_branch_qualification`. Author tree: `/home/drakestapleton/workspace/omega-security673-benchmark`, baseline `dd28ebc0a84abc4dab3a38bde49aeae98c846c78`, with three additive uncommitted qualification sources. No author source was changed by this reviewer.

**Disposition: all eight public synthetic cases pass, and both compiled weakening controls are detected. No new correctness defect was found within this bounded scope. This does not complete RSI#67, hidden-task protection, poisoned-benchmark qualification or promotion authorization.**

## Property and enforcing boundary

The executed candidate receives one public mode byte through its assigned input. It attempts to supply wrong, empty, oversized or self-approving output, return a wrong status, or read/truncate five synthetic files representing judge, policy, keys, receipts and budget. Each protected file contains only the 33-byte public string `SYNTHETIC-NONSECRET-QUALIFICATION`.

The qualification composes the existing `src/learning/benchmark/core.om` unchanged. Reviewer `cmp` against that file at the pinned commit exited0. Actual completed-child output feeds existing `lobserve` at core line124 through `security-boundary.om:193`. The new `readfile` loop at line61 reads until EOF or the 257-byte oversize sentinel. The independent expected-results table, protected-byte checks and kernel trace supplement the existing observer. They are not a new judge or promotion implementation.

The real process fence is existing compiler `seed/omc/omc.om:2405` (`hpure`) and line2533 (`hpack`), reviewed separately. Native Linux7.0.0-1019-nvidia AArch64 kernel traces show close_range, Landlock application, the 27-instruction seccomp filter and execveat for all eight children. The hostile child's ten open attempts, read and truncate for each of five files, returned EPERM. The parent checked every protected file after each case.

## Independently executed results

Snapshot: `/tmp/omega-benchmark-final-review.thgsgH`. Its `evidence/*.log`, `.exit`, `.strace`, `.err`, `identities.txt`, `case-artifacts.sha256` and ELF headers retain receipts.

| Mode / control | Child exit | Existing observer | Overall result |
| --- | --- | --- | --- |
| 0 correct byte | 0 | 0 | expected |
| 1 wrong byte | 0 | 7 | expected |
| 2 denied protected-file attacks plus wrong byte | 0 | 7 | expected |
| 3 empty output | 0 | 9 | expected |
| 4 wrong exit | 77 | 8 | expected |
| 5 257-byte output | 0 | 10 | expected |
| 6 self-claimed APPROVED text | 0 | 7 | expected |
| 7 correct byte after rejected cases | 0 | 0 | expected |
| Fixed eight-case consumer | | | exit0 |
| Compiled removal of observer byte-mismatch refusal | 0 at mode1 | incorrectly0 | consumer109 |
| Compiled execpure-to-execprog replacement | 61 at mode2 | 8 | consumer109 |

The second weakening control really executed the candidate and opened the disposable judge with O_TRUNC. Its judge became zero bytes; the fixed judge remained33. This is an observed attack at the kernel/file boundary, not a synthetic predicate returning a planned failure. The first control really accepted the wrong byte in unchanged observer context before the qualification rejected that result.

## Commands and identities

All build, readelf and composition commands below exited0. Executable permissions were added only after build/header success. `bin/omc` is the independently reconstructed stage2 from `/tmp/omega-pure-review.VkogeG`, not an unverified author-built candidate.

```sh
git show dd28ebc0a84abc4dab3a38bde49aeae98c846c78:src/learning/benchmark/core.om > src/pinned-core.om
cmp src/core.om src/pinned-core.om
cat src/core.om src/security-boundary.om > src/consumer.om
bin/omc < src/consumer.om > bin/consumer 2> evidence/build-consumer.err
bin/omc < src/security-candidate.om > bin/candidate 2> evidence/build-candidate.err
bin/omc < src/security-transform.om > bin/transform 2> evidence/build-transform.err
bin/transform 0 < src/consumer.om > src/mutant-0.om
bin/transform 1 < src/consumer.om > src/mutant-1.om
bin/omc < src/mutant-0.om > bin/mutant-0 2> evidence/build-mutant-0.err
bin/omc < src/mutant-1.om > bin/mutant-1 2> evidence/build-mutant-1.err
```

For each of `consumer`, `candidate`, `transform`, `mutant-0`, `mutant-1`: `readelf -h bin/NAME > evidence/NAME.header`, then `chmod +x bin/NAME`. Each case ran from its own fresh `cases/{fixed,observer-mutant,launch-mutant}` directory, with only an empty build directory initially. Exact process command, substituting the listed case/program pair:

```sh
timeout 15s strace -f -qq -o ../../evidence/CASE.strace -e trace=execve,execveat,openat,close_range,seccomp,landlock_restrict_self,read,write /tmp/omega-benchmark-final-review.thgsgH/bin/PROGRAM /tmp/omega-benchmark-final-review.thgsgH/bin/candidate > ../../evidence/CASE.log 2> ../../evidence/CASE.err
```

Pairs and actual exits: `fixed/consumer=0`, `observer-mutant/mutant-0=109`, `launch-mutant/mutant-1=109`. No timeout or unexpected signal was counted as success. All process stderr files were empty.

SHA256 identities:

| Artifact | SHA256 |
| --- | --- |
| unchanged core | `a5a2e74c815f9ca6a90dbdfbfc7ecc343f0362c824064f2058e9d1d7c119c40c` |
| boundary source | `3cbcc8e0484d4ab5849c63b43cd1d27986838420e29dc8fd77f58dac36e0ab29` |
| candidate source | `c780034abc20769d76f8d69febcefda67cd52f4f0ebe29af0a59b7a42fa63d00` |
| transform source | `e7584cf5bc4dcc5f4f4139a79ebf414dd8021e8beec328e4db38f343e309d545` |
| compiler source | `2e6e17680cfab319f28842fa68c9e6520e5412c4831f80ab109945087bdd0ad3` |
| compiler binary | `87542cfdf91e9666851b6e21d16c3a281547a8f7520dcedcd1f2a8e8540285e1` |
| fixed consumer | `5547012af86779e055e87ac3edbb0775f9d0f363b94c3e5b85808e343cdc7ff0` |
| candidate | `c631c7cbe218310f59d1e6b303148c1ddbaf0e86b37454835ea076205135e50b` |
| observer mutant | `c49c8f97ed66ad55a7cd6667cdfddd563db7137b58ba8143cba953b9aed5dcf5` |
| launcher mutant | `f7495e15b3ee08e8cde12a635931b04c4fc5f9d722de99d2d6872b771757df8a` |
| final evidence manifest | `76c25a2d8d21980062805403813d7e9e5506da7e9b8e94db6f0544660869ff80` |

The full manifest also identifies composed/mutated sources, transform binary, strace6.8 and timeout. Preserve this disposable snapshot as durable evidence before cleanup. Earlier pre-EOF-loop replay remains historical at `/tmp/omega-benchmark-review.iDRRFN`.

## Limits and remaining owners

This is independent review and native bounded testing, not diverse compilation, formal proof or physical device qualification. The observer and fixtures share the seeded compiler lineage. Strace supplies an external kernel observer; the expected-results table and real compiled weakening controls test verdict relevance. Hashes establish artifact identity only.

No private task, real key or holdout data was used. The test does not invoke persistent attempt/budget policy, signer or promotion paths, qualify statistical poisoning, protect against outside same-user writers, bind an artifact to immutable content, or prove revocation. Five public files model assets without establishing the real production storage boundary. Existing L775/evaluation/authority owners retain those requirements.

The previously identified host crash-collector blocker remains: RLIMIT_CORE0 does not disable a piped collector. Private-input/hidden-evaluator qualification requires the existing platform/evaluation owners to qualify controlled crash handling in a disposable environment before using such inputs. This replay did not intentionally crash candidates or inspect unrelated crash records.
