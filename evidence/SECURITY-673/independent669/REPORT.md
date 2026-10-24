# Independent review: corrected Omega #669 qualification slice

Date: 2026-10-23. Reviewer: `review_branch_qualification`, separate review agent.
Author tree: `/home/drakestapleton/workspace/omega-security673-qualified`.
Base commit: `05796f9e0dc72cf5cd21d64781d2a18e5e9ab71a`; reviewed changes were not yet committed.

**Verdict: no remaining finding in the tested branch-limit slice. The earlier P2 checker defect is fixed and independently retested. This is not approval or closure of all #669 acceptance criteria, #673, compiler trust, or host isolation.**

## Threat, property and enforcement

The input is synthetic source requiring conditional branches at or beyond the AArch64 signed imm19 range. The protected asset is correct emitted control flow. An out-of-range displacement must be refused before emitting an accepted artifact. No data theft, authority bypass, or machine takeover was demonstrated.

`seed/omc/omc.om:931` (`disp19`), `patch19`, and `blo` enforce alignment and the signed byte range -1048576 through +1048572. The corrected unsigned lower endpoint is 18446744073708503040. Inspection confirms that arithmetic for compiler positions within its 8 MiB output pool. This inspection is not a formal proof.

Supported execution tested here: Linux 7.0.0-1019-nvidia, AArch64 GNU/Linux. GNU objdump 2.42 is the external instruction decoder. No emulation or GPU/device activity was used.

## Independent results

Final snapshot and raw artifacts: `/tmp/omega669-final-review.UDT3ZV/`.
The author must preserve this directory in the durable evidence bundle before temporary files are reclaimed.

| Check | Observed result |
| --- | --- |
| Reviewer builds of final checker and runner | Both exited 0; ELF headers checked before execution |
| Complete native gate | Exit 0, `gate.log` ends in the bounded-testing PASS statement |
| Real helper probes | Four valid cases exited 0; two out-of-range and two unaligned cases exited 24 |
| Real short backward caller | Local-array initialization fixture compiled and executed with exit 0 |
| Exact forward maximum | Native execution exited 0; objdump branch at 0xc0 targets marker 0x1000bc, a displacement of 1048572 bytes |
| Forward overflow, signed wrap, nested-if, while-exit, trap-site layouts | Compiler exited 24 with empty artifact outputs |
| Weakened-limit compiler | Built successfully; independent decoder showed the branch target differed from the marker; checker exited 61; malformed image was not executed |
| Same weakened-limit source through fixed compiler | Refused with exit 24 and empty artifact output |
| Seven synthetic decoder cases | Good input 0; malformed token, wrong register, duplicate/missing marker, and `notmov` prefix 62; mismatched target 61 |
| Empty decoder output | Exit 62 |
| Supervisor wrong normal exit | Final runner exited 99 and logged `exit=7 expected=0` |
| Supervisor self-signal | Final runner exited 99 and logged child wait low-byte 9 |
| Supervisor deadline overrun | Final runner exited 99 and logged deadline exceeded, SIGKILL, wait low-byte 9 |

The array-fill artifact was independently disassembled: its `b.cc` / `b.lo` at 0xd8 targets the store at 0xcc. This exercises the actual `blo` caller. It does not create a reachable long backward source branch.

The runner was replayed independently from copied source in a fresh disposable directory. Normal-run files were subsequently moved to `build/security673-native-normal`; the paths printed in `gate.log` retain their original `build/security673-native` prefix. Control runs were retained in distinct directories. `gate.exit` is 0; `control0.exit`, `control1.exit`, and `control2.exit` are each 99. Raw child commands and exits are in `gate.log` and `control*.log`, with corresponding `.err` files.

## Review finding and correction

The initial checker accepted malformed target suffixes and arbitrary marker instructions. The first correction refused both original reproductions. Re-review found a remaining substring hole: `notmov` was accepted as `mov`. The final correction checks the preceding delimiter and integrates that case as the seventh synthetic negative control.

These exact commands were executed against the freshly rebuilt final checker, with final snapshot as the working directory:

```sh
printf 'b0: b.ne 0xb4garbage\nb4: bogus #12345oops\n' | ./checker
# checker exit 62
printf 'b0: b.ne 0xb4\nb4: mov x8, #12345\n' | ./checker
# checker exit 62
printf 'b0: b.ne 0xb4\nb4: notmov\tx1, #0x3039\n' | ./checker
# checker exit 62
```

An earlier review invocation used a scratch path the author had already moved and returned 127. That was an invalid invocation, not a successful refusal test. The final commands above use the reviewer's own rebuilt, identified checker.

## Raw reconstruction and execution commands

Final source files and `seed/rung4/om4.bin` were copied preserving their repository-relative paths into the final snapshot. The following builds used the existing author's identified stage2, with absolute snapshot paths in the actual invocation:

```sh
/home/drakestapleton/workspace/omega-security673-qualified/build/security673-correction1/stage2 < seed/qualification/security673/branch-check.om > checker 2> checker-build.err
# exit 0
readelf -h checker > /dev/null
# exit 0
chmod +x checker
# exit 0
/home/drakestapleton/workspace/omega-security673-qualified/build/security673-correction1/stage2 < seed/qualification/security673/gate669.om > gate669 2> gate-build.err
# exit 0
readelf -h gate669 > /dev/null
# exit 0
chmod +x gate669
# exit 0
./gate669 > gate.log 2> gate.err
# exit 0
mv build/security673-native build/security673-native-normal
./gate669 0 > control0.log 2> control0.err
# exit 99
mv build/security673-native build/security673-native-control0
./gate669 1 > control1.log 2> control1.err
# exit 99
mv build/security673-native build/security673-native-control1
./gate669 2 > control2.log 2> control2.err
# exit 99
/usr/bin/objdump -D -b binary -m aarch64 build/security673-native-normal/1024 > array-fill.decoded
# decoder exit 0; reviewer inspected backward array-fill branch
```

All normal pipeline compilation, fixture, decoding, execution, and expected-refusal commands are individually printed in `gate.log`. The gate reconstructs the compiler through existing om4, then compiles the compiler again; both compiler artifacts have the same hash. That equality demonstrates reproducibility within this lineage, not correctness.

## Final identities (SHA256)

| File | SHA256 |
| --- | --- |
| `seed/omc/omc.om` | `4c0a57497ac2046304365741b7a0de110056a1ac17ba03fa6127890dd66e3dd9` |
| `branch-check.om` | `3165c09cb7ed7523355ccaa1acbdc89be55c2882fbb61fc7f25594bd68deeca3` |
| `branch-corpus.om` | `a2672e4d018ca2ded61044af19bcde0ad7f8a3990429067aa2d5086ee681a413` |
| `branch-mutate.om` | `4bc9e252ca68ba185c284b9b9aed09dee83526abae9913db6c6b592700db76e1` |
| `gate669.om` | `07bc11a69f0d629e0c262056061789114d8710505a308e3e2a5ac42ae73dee94` |
| `array-fill.om` | `bcfb200a48291c27d4a700ea074e02616a881767c8838a52ad2dfd7b83b52d33` |
| `oracle-corpus.om` | `41118388d232dd6897bebf9b239f4174a2e5054cdd07a4753c4ce60bf7c022fa` |
| `gate-child.om` | `459a60da8e660ee1f433a9679bb41467074fda45997e5168e23e92ebad0d570b` |
| Existing `seed/rung4/om4.bin` | `d0da40e45f1fcc45d9a39c9c52c9d745b545e076970729e51c12d76ef519efab` |
| Rebuilt compiler stages 1 and 2 | `7c25129a0a8ec0604f93ec51eb2028ecfc6e04dde936a73aeaa5f24a2b3214b8` |
| Reviewer `checker` and gate-built checker | `14140f4cb6bea232b6232bcc82b4760079e5e934f5997409ef8ab2a5a3ceb01d` |
| Reviewer `gate669` | `04247ac6436ac176f98fd0b9a205fe8f51e02c6ded7ac81dba3e9e113c5a4fc2` |
| `/usr/bin/objdump` | `f2e917a5d118ebf5679b24740e20d30338308e7f3f0630527c469de452d5a250` |
| Array-fill binary, normal run `1024` | `5fb117f43f28746f7090cbafe0e7fdd3ff885c31d23e14c3019916397847a5fa` |
| Maximum-forward binary, normal run `1056` | `546ca3f4776e7a32e4eb2daca0a5e52a9c466c6fbc5baf76f9dbb1fa0b846b33` |
| Maximum-forward disassembly, normal run `1057` | `852d7e0e680c5095b2c4dfe00fb95666354245ae4289b22a92e12773baa30910` |
| Mutant binary, normal run `1079` | `8cbdc67a54eb0a352e04dbd58e7cc1a5d872d19387f54955f6726f468ec3634e` |
| Mutant disassembly, normal run `1080` | `770ee3cdd4e7f0c25185702c33c4e9c08463454606dc6b61734fe7bc720ef4ef` |

## Residual assumptions and exclusions

- The runner/checker/fixtures use the same compiler lineage. GNU objdump supplies independent decoding; the `.om` checker is not an independent compiler implementation. No diverse-double-compilation or general source-to-binary correctness claim is established.
- The checker recognizes selected GNU 2.42 output tokens. It rejects the enumerated malformed cases, missing markers and duplicate markers. It is not a full disassembly grammar validator or authentication mechanism for arbitrary forged decoder output. The external decoder and local evidence directory remain trusted.
- The supervisor observes direct-child exits, rejects signals, and kills a direct child after its configured deadline. This is not process-tree confinement, memory/output quota enforcement, protection from same-user modification, or least-authority execution. Those remain #670/#671 work.
- Only synthetic source and disposable files were used. No deployment, live-driver change, GPU interruption, private upload, or automatic promotion occurred.
- This review does not cover the shell-runner retirement, historical-evidence restoration, fresh full seed-chain reconstruction, future host changes, release integration, or the other #673 requirements. Any later source change needs appropriate follow-up qualification and a new identity record.

Only this report was written outside disposable review directories. No tracked implementation or test source was changed by the reviewer.

Additive later review correction: the earlier supervisor control cases unconditionally returned99
on their final path, so those observed exits alone did not demonstrate sensitivity to an incorrectly
accepted child. The successor coordinator removes forced failure and qualifies a separately
compiled normal-exit/signal-confusion mutant. See `../pure4/REPORT.md` and the strict-profile
independent report. Earlier raw evidence remains preserved; no original run is relabeled.
