# L728 independent review — UAP1 L715

## Scope and exact pins
- Reviewed repository `/home/drakestapleton/workspace/hermes-desk/ua-profile-L715`, source commit `f71d0718e96bf849157619ea6d42d3616e4769d7`, evidence-only head `6f270361dace2a91e78255c05eb16d151216edbf`, base `a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a`. Worktree was clean at review.
- Source SHA-256: `core.om` `0a54c40c994dac6d02ca39d00eba4a6841ffaef6be5afe78d6a366752ddc40ef`; `cli.om` `bf00a7d3b816b59ca9394f7a5ba9e001c17356e79651526ed23999b046ba1f20`; `unit.om` `6312131acdb8acd6c482f4b6c1b86504149b840355ea74c4fc812ad916636a2a`; `CONTRACT.md` `3747fa7d7e3b737ec8a83162901853159a141b9e337d4683429cdcbdc65df73e`; `README.md` `1c37a51007e354ef44adceef77fc5cd251919d3b090502c55ce4b481211bb6f2`.
- Compiler pin declared by source: `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`. Replayed CLI binary SHA-256 `8acb94458753269be0f24bdd89f137a16378d7bcc628defcd0f6c4f4f3ecc045`; valid frame SHA-256 `dd9ca041e7607eafb2f4e75938684377cf76fef63de2033dcf8b99a13ed2b514`.

## Strengths
- `core.om:9-28` checks exact 288-byte length, magic, and reserved bytes; enum/structural validation precedes semantic categories. Pin checks are bytewise exact (`core.om:68-80`), and the acceptance paths do not rewrite the caller buffer.
- Validation rejects impossible runtime, native-Aienux, foreign-runtime and unknown-target relabels rather than presenting them as native success (`core.om:82-107`; `cli.om:32-54`). Output data explicitly holds `execution_authorized:false`, `fresh_execution:NOT_RUN`, explain-only and required revalidation steps (`cli.om:3-4`).
- `cli.om:21-54` reads at most 289 bytes, loops over short reads, treats zero as EOF, refuses non-288/trailing bytes before emitting JSON, and loops for short output writes. Diagnostics and refusals produce no stdout before validation.
- Existing native direct gate executed from the replay: exit 0, `0/1204 UAP1 direct checks`. Independent focused CLI probes exercised valid, empty EOF, 1- and 287-byte truncations, valid-plus-one, 4096 extra bytes, native-Aienux metadata-only, foreign-runtime metadata-only, unknown target, modified compiler pin and missing evidence. All outcomes matched expected exit codes; every refusal had empty stdout and named stderr. Raw output: `L728-review/probe-cli.log`, `L728-review/unit.log`.
- README makes public installer/distribution integration, native-Aienux, GPU, actual runtime checks and authenticated launch explicit non-claims. Existing target labels are disclosed as candidates, not measurements.

## Findings
No Critical, Important or Minor source defect found in the bounded plan-only behavior reviewed. This is not an approval for use as a trust boundary or launcher.

## Declined to judge
- Authentication/authority of `evidence selector=1` or caller pins: the code and contract correctly defer this; no receipt verifier or registry exists.
- Actual OS/hardware identity, target-specific execution, Pi5 qualification, native-Aienux, GPU/FFI, runtime compatibility, installed-registry lifecycle, generation/stale-handle semantics and launch integration: outside this module and explicitly unimplemented.
- Full package suite and source-origin/release publication: inherited/reported evidence only; this review did not rerun the broad suite.

## Verdict / limits
Bounded source verdict: the fixed-frame validator and plan-only CLI behavior are coherent with the stated scope, and focused replay probes passed. This does not close issue642/parent295, authenticate a profile, authorize execution, qualify either physical target, or establish normal distribution/judge integration. Parent disposition remains required.
