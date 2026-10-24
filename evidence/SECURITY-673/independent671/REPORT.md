# Independent review: bounded #671 compiler-source campaign

Date: 2026-10-23. Reviewer: `review_branch_qualification`, separate review agent.
Author tree: `/home/drakestapleton/workspace/omega-security673-qualified`.
Base: `12d971c7a5d4d6eb14b8ec7768b43d88e241374c`. The campaign changes were uncommitted when copied for review; the exact source identities below define the reviewed snapshot.

**Verdict: no remaining finding in the final18-case deterministic source campaign. Independent mode3 replay and the normal #669 regression both exited0. This does not close #671 or qualify arbitrary hostile input, comprehensive fuzzing, other ingress paths, or system-wide resource limits.**

## Threat, asset, enforcing paths and oracle

The tested threat is malformed or oversized source causing incorrect acceptance, incomplete output, or compiler failure while consuming its bounded parser tables and input pool. Protected assets are compiler availability and correct refusal before an artifact is accepted.

Existing enforcement is in `seed/omc/omc.om`: `tok_num` at157 (20-digit limit), `tok_text` at208 (quoted text), `pushb` at1123 (512 blocks), `arrlen` at1151 (array length), `rdval` around2649 (1024 value frames), `s_fn` at3092 (512 functions), and `main` at3975 (input capacity). This campaign tests existing compiler behavior; it adds no new parser implementation or production fallback.

The preexisting seeded profile in `docs/one_layer/OMEGESE.md`, sections9.3,9.4,9.6, independently states numeric, array and capacity limits. The nested-expression oracle is elementary boolean negation: an even number of `not` operations preserves the true comparison, returning0; an odd number selects the other arm, returning1. The final campaign includes both outcomes. This is a specification/math oracle for the enumerated cases, not a second independent compiler or general semantics proof.

Supported platform tested: Linux7.0.0-1019-nvidia, AArch64 GNU/Linux. Native execution only. No emulation, device or GPU operation was used.

## Final case results

All18 cases matched their declared compiler status. Each of the8 accepted fixed-compiler programs was executed, with the exit shown. All10 refused compiler outputs were verified empty by the coordinator.

| Synthetic source | Compiler exit | Program exit |
| --- | --- | --- |
| 20-digit zero token | 0 | 0 |
| 21-digit zero token | 6 | Not executed; empty artifact |
| 1,000,001-digit token | 6 | Not executed; empty artifact |
| 1000 nested `not` operators | 0 | 0 |
| 1021 nested `not` operators | 0 | 1 |
| 1022 nested `not` operators, maximum for this fixture | 0 | 0 |
| 1023 nested `not` operators | 14 | Not executed; empty artifact |
| 512 nested blocks | 0 | 0 |
| 513 nested blocks | 14 | Not executed; empty artifact |
| 1,000,001-byte quoted literal | 0 | 0 |
| 511 extra functions plus main | 0 | 0 |
| 512 extra functions plus main | 14 | Not executed; empty artifact |
| NUL input byte | 16 | Not executed; empty artifact |
| Array length16777217 | 23 | Not executed; empty artifact |
| Unterminated quote | 22 | Not executed; empty artifact |
| Unknown value name | 3 | Not executed; empty artifact |
| 1,000,001 leading whitespace bytes | 0 | 0 |
| 8MiB leading whitespace plus program, beyond input capacity | 14 | Not executed; empty artifact |

These are18 cases across a smaller set of source families, not18 distinct parser families. The 1022-operator boundary includes the fixture's comparison and operands within the documented value-frame limit; it is not a claim that the general profile permits only1022 levels.

## Meaningful negative control

The `.om` mutator mode3 changed exactly one compiler guard from `if gt ndig 20` to `if gt ndig 21`. Independent source diff confirmed that sole change; `git diff --no-index` exited1 to report the difference, not a security-check failure.

The transformed compiler compiled successfully. On the same21-zero token fixture, it returned0 and emitted a216-byte ELF; the fixed compiler returned6 and emitted0 bytes. The preexisting20-digit profile makes the mutant's acceptance a detected contract violation. This is a compiled control, not an imagined mutation or helper predicate. The accepted mutant artifact was format-inspected only and was not executed. The ordinary accepted fixed-compiler programs were all executed.

During review, the initial accepted nesting cases covered even parity only. The author added the1021-operator case expecting1; the independent final run confirmed that opposite outcome. No compiler defect was found by that coverage improvement.

## Independent replay and raw commands

Snapshot and raw artifacts: `/tmp/omega671-review.ezC3u5/`. Preserve this entire directory with the durable evidence before temporary files are reclaimed.

Compiler source, qualification `.om` files, existing om4 and the profile were copied into this fresh disposable directory. The runner was built with the independently reconstructed #670 compiler, whose binary hash is recorded below. These commands completed with the stated exits:

```sh
/tmp/omega670-review.bScRBA/bin/stage2 < /tmp/omega671-review.ezC3u5/seed/qualification/security673/gate669.om > /tmp/omega671-review.ezC3u5/gate669 2> /tmp/omega671-review.ezC3u5/gate-build.err
# exit 0
readelf -h /tmp/omega671-review.ezC3u5/gate669 > /dev/null
# exit 0
chmod +x /tmp/omega671-review.ezC3u5/gate669
# exit 0
cd /tmp/omega671-review.ezC3u5
/usr/bin/time -v -o hostile.resources ./gate669 3 > hostile.log 2> hostile.err
# exit 0
mv build/security673-native build/security673-native-hostile
# exit 0; preserve the complete first run
/usr/bin/time -v -o branch.resources ./gate669 > branch.log 2> branch.err
# exit 0
sha256sum build/security673-native-hostile/* > hostile-artifacts.sha256
# exit 0
sha256sum build/security673-native/* > branch-artifacts.sha256
# exit 0
git diff --no-index seed/omc/omc.om build/security673-native-hostile/1067
# exit 1; only the numeric guard20->21 differs
wc -c build/security673-native-hostile/1071 build/security673-native-hostile/1072
# exit 0; 216 bytes mutant artifact, 0 bytes fixed refusal
```

`hostile.log` and `branch.log` contain every child command, input/output/error path, measured child elapsed interval, actual exit and expected exit. The source run ended with `GATE671 SOURCE PASS bounded synthetic source families only`; the regression ended with the bounded #669 PASS statement. The source run's printed paths retain their original `build/security673-native` prefix; its files were subsequently preserved under `build/security673-native-hostile`.

The normal #669 replay exercises the final expanded mutator's original modes and the compiler containing the #670 exec change. It passed the branch boundaries, actual backward-array caller, independent decoder controls and branch-range mutant. GNU objdump2.42 supplies the separate decoding boundary for that regression.

## Resource observations and failed attempts

GNU Time reported0.09 seconds elapsed,0.03 user seconds,0.02 system seconds and8160KiB maximum resident set size for the source campaign. It reported1.79 seconds elapsed and8160KiB maximum resident set size for the normal branch regression. The largest child interval logged by the source coordinator was8463834ns. These are observations from one run, not performance guarantees, memory quotas, total concurrent-memory bounds or proof of denial-of-service resistance. GNU Time reports its version as `UNKNOWN`; its exact binary hash is recorded below.

The coordinator's previously reviewed direct-child30-second deadline remains in place. The campaign does not add process-tree isolation, kernel memory limits, output quotas, or sandbox enforcement.

Author failures remain excluded from acceptance: `gen-build.err` records error13; earlier `gate1.log` and `gate2.log` record unexpected compiler exits11 and9, with gate exit99. Those invalid fixtures were corrected before this final snapshot. Neither their build errors nor failed executions count as passing security checks. The independent final replay had no unexpected crash, build failure, missing run, signal, or timeout.

## Identities (SHA256)

`identities.txt` identifies all copied qualification sources, the profile, runner and external tools. `hostile-artifacts.sha256` and `branch-artifacts.sha256` identify every generated fixture, compiled binary, output and stderr file in each run.

| Artifact | SHA256 |
| --- | --- |
| Compiler source, unchanged from #670 review | `33ba46ab1fce0e2cab8f027187f55361bb9a949d597029454ca6641a756f65ac` |
| `hostile-corpus.om` | `0590fa1f3ec5d6119dae81cdbdaba51cdcd50dba21eacd7d50c743e92024e8fb` |
| Final `gate669.om` | `c04e986c01ef5132cd5d578a170cc941e8c1868ad427277a7536b7a959a55b2d` |
| Final `branch-mutate.om` | `94285a3a6cfcf20162e551d491c9a2ed7d18b13106f55acbed8b5f9310e9f9e6` |
| Seeded language profile | `a85349d20ef20a4938ae8d4eebe1651b00fe5b9d6a110da83f3c386210514047` |
| Existing om4 | `d0da40e45f1fcc45d9a39c9c52c9d745b545e076970729e51c12d76ef519efab` |
| Compiler stages1/2 and reviewer runner-builder | `6b5d94033b9705d78c8e85e806612aac5a94d90f217c54a97fe1891a2ce5aa55` |
| Runner binary | `03b8cdbf5fa7f9de45960d0d27d5adf0a62bda9be58508c9efb859dd4541f522` |
| Hostile fixture generator binary | `ee831be16f8bc39c70e02046d22bfe58ec09640b7d06b8b8c3aa9f60d4d8c305` |
| Mutant compiler source | `a87e8e1707194a813cf2a0bf5794b0b2521e01a5e181aca24c5d4af4456c1a41` |
| Mutant compiler binary | `49fec1a7fb310e5cd2f00e5e9d7d49ba7891028dd330a7ec9ef7bc996553e740` |
|21-digit negative-control fixture | `7919f2bd33fed2a20026f2f3956a5c2d786b5a26522b5503d938f8a219891464` |
|216-byte mutant artifact | `6d02cbf1e06350934eae247a1ec3df2746deba5064d955b0505043c786209898` |
| Source-run artifact manifest | `01c6e0170840694ad04ef68ff2143ac0deea616e8671a8383dad07521b5ba6a1` |
| Branch-run artifact manifest | `99f8e24754fe5307f7016bedbda44945af5e595e5893a87413195bc62fb3927d` |
| Source log | `e92e573e877b03150a8119afdbd4a856e7a8cfa5c90a6a8add59203c1f793c19` |
| Branch log | `becd55cc50c6078f1c88e0aaef43cc3982ab46deba664832b1c38f71b10212d9` |
| `/usr/bin/time` | `4a445f72fefaf4127931df2b799aa885e27553b5a55e104a329d41fc8dc83083` |
| `/usr/bin/objdump` | `f2e917a5d118ebf5679b24740e20d30338308e7f3f0630527c469de452d5a250` |

Hashes bind the observations to artifacts. They do not establish correctness.

## Remaining scope

- This is a bounded deterministic campaign, not comprehensive fuzzing, an exhaustive parser proof, or qualification of all #671 ingress paths. Network protocols, serialized authority/store records, device input and other parsers are outside this review.
- All first-party generator, mutator and verdict logic inspected here is `.om`, compiled through the existing seed lineage. The lineage remains shared, so correlated compiler defects remain an assumption. Profile bounds and parity provide independently stated expectations, not diverse compilation.
- No private data, live services, drivers, devices or model workloads were used. No production source was modified by this campaign, and no source was edited by the reviewer. No deployment, GitHub action or automatic promotion occurred.
- Existing #669/#670 reviews and their residual limits remain applicable. This report does not approve the entire security workstream, all compiler semantics, or release readiness.
