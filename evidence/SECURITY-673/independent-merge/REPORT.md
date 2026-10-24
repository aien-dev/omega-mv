# Independent review: merged ingress tests and strict-runner regression

Date: 2026-10-23. Reviewer: `review_branch_qualification`. Exact author revision: `c860d91563bb234a8d6955b0caca85c010f5bc06` in `/home/drakestapleton/workspace/omega-security673-reviewed`. Both concurrent histories are preserved. Reviewer made no author-tree changes.

**Disposition: final normal gate and strict mode4 pass; normal-exit152 signal spoof is rejected with99. All seven new rename and seven path/argument cases were separately traced. No new correctness defect was found within these bounded properties. This extends earlier reviews; it does not close #671 or certify general filesystem authority.**

## Exact properties and observations

The threat is malformed length-delimited paths, rename packets and argument-buffer requests crossing existing compiler/runtime boundaries. Protected assets are parser/buffer bounds and correspondence between the supplied paths and actual kernel operation. Existing enforcement remains `seed/omc/omc.om:2106` (`hpathcp`), `hpath`, argument lowering and line2533 (`hpack`). The production compiler source is unchanged from the separate execpure review.

Inspection confirms the corrected tests now assert the behavior they claim: flag1 for rename without replacement, source absence and exact destination contents after success, then EEXIST on a second rename. Argument tests require the exact error for short capacity, an unchanged sentinel, exact-capacity byte equality and an unchanged following sentinel. Empty pathname requires exact ENOENT.

The independent trace of rename mode0 shows real `renameat2(..., RENAME_NOREPLACE)=0`, source `openat=-ENOENT`, destination read of exactly `x`, then a second real rename returning EEXIST. Six malformed modes exit103 before any renameat2; each creates only its synthetic source first. They cover wrong record counts, empty first/second path, trailing byte and embedded NUL. These cases do not establish exhaustive packet grammar coverage.

Path mode0 makes the expected file; modes1/2 (embedded NUL/4096-byte path) exit103 before openat; empty mode3 actually calls openat on the empty path and receives ENOENT. Short argument mode4 returns0 after exact error and unchanged sentinel; absent argument mode5 traps103; exact-capacity mode6 returns0 after exact bytes and following sentinel checks. Successful argument tests do not make path syscalls. This does not qualify arbitrary pathname authority or races.

## Independent execution and receipts

Fresh snapshot `/tmp/omega-merge-gate-review.w6oWLA` contains copied sources and om4, runner, all generated programs, logs, exit files, headers and source/binary/artifact manifests. Native platform: Linux7.0.0-1019-nvidia AArch64, GNU objdump2.42, strace6.8. These are CPU/kernel tests; no device, model or private input was used.

```sh
./compiler < seed/qualification/security673/gate669.om > gate 2> evidence/gate-build.err
readelf -h gate > evidence/gate.header
chmod +x gate
timeout 150s ./gate > evidence/normal.log 2> evidence/normal.err
timeout 90s ./gate 4 > evidence/strict.log 2> evidence/strict.err
timeout 30s ./gate 5 > evidence/spoof.log 2> evidence/spoof.err
```

Build/header/chmod exited0. Runs respectively exited0,0,99. Scratch was moved to `build/security673-native-normal` and `...-strict` between runs; spoof scratch remains `build/security673-native`. Every nested command and actual/expected exit is in the gate logs. Both compiler rebuild stages matched the independently reconstructed compiler binary. Strict mode retained the meaningful seccomp/Landlock compiled bypasses and planned SIGXCPU checks. The prior normal-exit rejection remains present at runner line230. Mode5's99 is the actual rejection, not an unconditional final failure.

For separate tracing, each mode used its own fresh `cases/rename-M/build/security673-native` or `cases/path-M/build/security673-native`. Rename input IDs are `1141+3*M`; path input IDs are `1162+2*M`, for M0..6. These are actual `.om`-generated input artifacts from the completed normal gate.

```sh
timeout 5s strace -qq -o ../../evidence/trace/rename-M.strace -e trace=renameat2,openat,read,write /tmp/omega-merge-gate-review.w6oWLA/build/security673-native-normal/1139 < /tmp/omega-merge-gate-review.w6oWLA/build/security673-native-normal/INPUT > ../../evidence/trace/rename-M.out 2> ../../evidence/trace/rename-M.err
timeout 5s strace -qq -o ../../evidence/trace/path-M.strace -e trace=openat,read,write /tmp/omega-merge-gate-review.w6oWLA/build/security673-native-normal/1161 seed/omc/omc.om < /tmp/omega-merge-gate-review.w6oWLA/build/security673-native-normal/INPUT > ../../evidence/trace/path-M.out 2> ../../evidence/trace/path-M.err
```

Actual rename exits M0..6: `0,103,103,103,103,103,103`. Actual path exits: `0,103,103,0,0,103,0`. No missing run, timeout, build failure or unexpected signal counted as a pass. The intentionally planned SIGXCPU is distinguished from the rejected normal152 spoof.

SHA256 identities:

| Artifact | SHA256 |
| --- | --- |
| compiler source | `2e6e17680cfab319f28842fa68c9e6520e5412c4831f80ab109945087bdd0ad3` |
| compiler stages1/2 | `87542cfdf91e9666851b6e21d16c3a281547a8f7520dcedcd1f2a8e8540285e1` |
| gate source | `c84ccfddfb16f19312d10ac7b682cd6d625aef5b6a38d0ae162ba0c4676e8965` |
| gate binary | `e6b9e4dd83588a9869d43f700cc8f4880b668886e27b0dd9ee658cb9f5b15c58` |
| mutator source | `2c0328d114ff622af869c5a68bcf013292a9f5d938c507df1db98fd1c16f234d` |
| rename corpus source | `9c44348b9cb258195be17e59318d90def4745327baed621b0fc46442f50ad883` |
| rename consumer source | `cf6a1089ca4c388444e9ce2efc31fc98ae35d0fa77cf939a255e84de78a0c9e6` |
| path consumer source | `81921cb1c7fb8920fdd200f25f7f6a5b73a01453bc07bb1f5a551f20a71833b7` |
| rename consumer binary1139 | `8d8c6a5ffc927449400c2c0f8cf94cb9ff7e8b78b5b94b2f4f387d6b77312218` |
| path consumer binary1161 | `a3b7a6bfe01320a280b59eee969db6520730fdcaf77cdfce9f7f188bd3731903` |
| final evidence manifest | `74240e1f932ea34559ab884321e4bd21d36a9cf461516c4a075fddb3935bec86` |

`evidence/identities.txt` also identifies all fixture sources, seed om4 and decoder. Per-run artifact manifests retain all generated binaries, inputs and outputs. `evidence/trace-artifacts.sha256` and `trace-case-artifacts.sha256` identify trace receipts and final disposable files. Preserve the snapshot as durable evidence before cleanup.

## Residual limits

This review supplies independent inspection, native execution and external kernel observation. It is not full grammar fuzzing, semantics proof, diverse compiler trust or hardware qualification. New path/rename negative cases exercise real consumer traps; no new compiled mutation of those specific guards was added in this bounded slice. Existing compiler guards, observer tooling and runtime remain assumptions beyond the tested cases.

Prior `paths1`/`renames1` reports are historical, with older source/assertions. The merge report and this exact snapshot identify the strengthened current tests. They must not silently rewrite historical outcomes.

The host crash collector remains unqualified for private inputs. Existing execpure stdio-authority, inode/content-identity, revocation, durable-budget and supervisor limitations remain as recorded in `qualified-execpure-review.md`. Nothing here establishes hidden-evaluator confinement or authorizes deployment, host policy changes or automatic promotion.
