# L552 independent corrective review

All three exact corrective artifacts receive **ACCEPT_HOST_SCOPE**. No new actionable defect was found in these deltas. The prior decoder HOLD is resolved for the declared static, caller-owned export boundary. These are independent review recommendations for desk intake, not whole-project acceptance, a merge approval, or issue closure.

| Artifact | Exact corrective head | Reviewed base | Verdict |
| --- | --- | --- | --- |
| Decoder linked-input refusal | `676a0b80ef71fba1cf5360da2e6cb9186789dfb5` | `9d8807ddc306e204faa20ce2faeb48e215cdb5af` | ACCEPT_HOST_SCOPE |
| Native600 protocol | `9869cbcb9eb7e2b39673a4208785606826385cd9` | `dc24fcf27bbc6c36ca6dfcac020452bb06004bae` | ACCEPT_HOST_SCOPE |
| Trace startup refusal | `a00ef9b5089fd37876caece268e2be9601d20158` | `2dee6673dc6df329ee4b4913069affd1ea307e29` | ACCEPT_HOST_SCOPE |

Reviewer L552 authored no implementation change. Prior independent L541 review at `c2c31059865eff6073ffeae819d0199b2d0a55bb` was read; its unchanged arithmetic/source audit and full decoder work were not repeated wholesale. All three exact public author refs were independently verified. Only own review evidence is committed.

## Resolved findings and independent reproduction

### Decoder B1, prior P2: resolved

Location at corrective head: `tests/compiler/verify_decoder_corpus.py:12-18`, `:35`, `:43-47`, `:52`. The root remains unresolved until checked; every component from that root to the four declared corpus directories, every source and loader is checked for symlinks. Regular source leaves must have link count one. Exact pinned Git membership and blob checks remain intact. The wrapper and production decoder source have no delta.

Actual differential: run the new 13-case harness against the exact old verifier in an owned harness directory. Exit1, seven failed refusal assertions; the same harness with corrected source exits0, 13/13. Root/ancestor relocations retain identical bytes, including the loader. Original67 completeness/wrong-byte controls still pass unchanged. Public `decoder-red.json`, `decoder-green.json`, `decoder-original67-cases.json` and command records retain the cases and exits.

Additional independent `decoder_extra.py` checks six cases: ordinary export; linked compiler ancestor; changed bytes behind that link; multiply linked loader; a trusted ancestor outside the chosen root; and restored ordinary export. All pass. Before/after snapshots show the verifier does not change file bytes, names, modes or link counts, including the relocated target. Fixture corruption is performed explicitly by the test, never by the verifier. Ordinary and restored exports still enumerate59 programs plus the loader. The trusted outside ancestor succeeds as the stated contract permits; no broader tenant-isolation guarantee is imposed.

The parent packet SHA256 list verifies. Its retained full plain/ASan logs each show59 units,59 roundtrips,0 compile refusals,22 refusal cases,5 oracle controls and10 successfully built/caught decoder mutants. Pre/post corpus manifests match. These full decoder builds were **inspected, not rerun by L552**. L541 had independently rerun the unchanged decoder before this verifier-only correction. Historical PR499 pin `1e4cdbe91b444efe51af4e745f366e07d54de030` remains the only decoder qualification scope. Current v6 is not qualified here.

Boundary: static trusted caller-owned root and descendants only. Ancestors outside that root and hostile concurrent writers remain outside the contract. This is a resolved input-containment defect, not a discovered decoder semantic defect.

### Native A1/A2, prior P3 each: resolved

Locations: `research/exploration-600/continuation/guard.om:53-58` and `CONTRACT.md:11`. The two nested checks admit VT/FF alongside HT/LF/CR/space. The contract now explicitly requires EOF framing and at most16,383 bytes, including separators, padding and leading zeroes. At16,384 bytes the preexisting bounded read logic refuses20 without output. Numeric/accounting/admission code is unchanged.

The reused exact corrected ELF SHA256 is `b5afc755b60012ed907567c8670739bebdeb68727478d4e8c0602174a279bc43`. It was verified before copying to L552's owned output and checked again. Qualified seed compiler SHA256 `01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6` also matches. No compiler was created or changed; Python remains a test oracle.

Actual differential: the16 protocol tests on the old hash-qualified native binary fail three assertions (VT, FF, mixed whitespace), exit1. Corrected binary passes16/16 and all original423 numeric/protocol checks. Independent342 additional checks pass:246 nondigit separator-byte cases covering all other byte values,24 prefix/suffix padding boundaries across all six allowed separators,2 leading-zero length boundaries,6 arity/overflow/empty controls and64 deterministic fragmented pipe inputs with mixed whitespace and leading zeroes. Valid cases require exact four-line output; invalid cases require exit20 and zero output. This includes16,383 acceptance and16,384 refusal with every accepted whitespace byte.

All five existing native behavioral mutant ELFs were independently hash-verified against the published compile-success records and rerun: each exits1 on real failed oracle assertions. L552 reused those compiled artifacts; it did not compile them again. The initial parent's rejected combined-expression compile is retained historical failure, never counted as a mutant kill.

Fresh replay produces600 rows, byte-identical to both the corrected gzip packet and original `dc24fcf2:evidence/EX600-CONTINUATION-L523/counterfactual-rows.csv`, including CRLF bytes. Git diff also confirms that all preexisting evidence and original policy/freeze/heldout inputs remain unchanged. Parent protocol source and packet SHA256 lists verify. Original2337 checks,40 diagnostics and six original controls are **inspected prior evidence, not rerun by L552**.

Economic interpretation stays unchanged: at H32 cap160 the C replay nets38,190 synthetic units with320 decision overhead and no added discoveries, below the original38,510; cap32 nets-590 and loses20 useful discoveries. The default guard remains REJECTED, original scientific verdict FAIL. No new optimizer, economic success, fresh heldout science, physical timing or energy claim follows from protocol compatibility.

### Trace synchronous reservation refusal: resolved

Location at corrective head: `tools/trace/archive.mjs:134-137`, with existing producer refusal at `:163-170`. Reservation failure is recorded synchronously before returning to a producer; the existing error gate refuses submission before worker error delivery. Worker failure reporting and baseline archive/recovery behavior remain active.

Independent `startup_probe.mjs` injects a real `fs.fsyncSync` EIO at either of the first two reservation sync calls against owned archives containing one previously durable event. On exact old source both immediate submissions return true, submitted2/pending2, while the archive still reads PASS. On corrected source both return false, submitted0/pending0; errors are visible immediately and close is nondurable. The same fixed-expectation probe on old source exits1 with ERR_ASSERTION, independently demonstrating a red-capable regression. Corrected probe exits0.

In both versions the baseline archive bytes stay identical and its original event remains present. Corrected recovery preserves that event and reads PASS. A baseline PASS is legitimate after refusal because no new work was accepted; it cannot conceal accepted pending work in these corrected cases. Additional probes cover an already occupied synthetic writer session (refusal preserves its marker), missing parent (no parent creation, accepted0), and healthy startup (active reader INCONCLUSIVE, accepted1/persisted1, durable close and final PASS).

Fresh full `tests/trace/run.sh` on the exact corrective export passes51/51 tests, zero skips, native oracle12 assertions, kernel-denied offline workflow8 checks and deliberate EPERM denial control. Both mutation runners pass:17/17 original and9/9 follow-on mutants fail behavioral assertions; the original false-persistence mutant still exercises the strengthened real asynchronous failure case. A separate seccomp-denied `review-offline.mjs` passes14 checks and `strace -f -qq -e trace=network` records zero bytes. No retries, assertion weakening or compile/import failures were used as mutant success.

## Evidence, failures and limits

`commands.json` records core argv/cwd/exits; `commands.md` records the remaining actual commands, output ownership and reuse. Machine/tool and executable hashes are retained. Source deltas are exact Git diffs. `packet-verification.json` records immutable source/evidence checks and five mutant reruns. Detailed26 trace mutant assertion logs and exits are public beside this review.

Review-helper failure: the first packet-verification helper expected the text `10/10` while the retained decoder log actually says `10 of 10 caught`. Its assertion exit1 is preserved in `packet-verification-initial-failure.log`. The helper was corrected to require that actual exact gate line; final verification exits0. This was a review-parser error, not an author failure. Original red behavior and parent historical compile/packaging failures remain intact.

Known open qualification gaps are not newly found defects: current35B/shared-train hardware, real training integration, native callback/OSC ABI integration, browser/cross-project integration beyond the existing host suite, fresh heldout outcomes, runtime promotion, whole-repository acceptance and physical performance remain NOT_RUN. Energy is UNMEASURED. No hardware/model/service, live18091, PR/issue write, closure or other owner's source/worktree was changed. L553 continues separate backlog work; L487/L488 retain integration and closure authority. Desk disposition is REVIEW only.

Publication note: raw assertion logs and exact Git patches that contain whitespace-only diagnostic/context lines are stored losslessly as `.gz`. `lossless-publication.json` pins their original bytes. The initial whitespace-check exit2 is retained separately; the check is applied normally after lossless packaging. This does not alter any test result or author evidence.
