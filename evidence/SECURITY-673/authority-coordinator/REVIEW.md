# Authority regressions in the existing security coordinator

Source-only review, 2026-10-24. Publication base:
`f4fbb2a7f1419ac49e890a1b8c37f20af02dac38` (stacked #688).

## Change and boundary

`seed/qualification/security673/gate669.om` adds the three existing committed
regressions at name IDs 34–36, without renumbering the preceding 33 entries:

| Gate | Expected child output | Expected exit |
| --- | --- | --- |
| dispatch-order | `dispatch order checks=3 fails=0` | 0 |
| store-exact | `store exact checks=4 fails=0` | 0 |
| store-cleanup | `store cleanup cases=18 fails=0` | 0 |

The same twice-rebuilt compiler, artifact checks, fork/exec launcher, wait-status
checks and deadline handle these children. There is no new runner or production
implementation. Compiler, production authority/store/lock code, trust policy,
signing and previous evidence files are unchanged. Existing owner integration
(L408 and Renda review) is retained.

Each new run gets an exclusively created mode-0700 numbered directory inside
fresh coordinator scratch. Only its child changes directory, so malformed
store-exact outputs and other fixtures cannot contaminate the other regressions
or earlier authority fixtures. Existing gates keep their prior behavior.
The compiler artifact is passed as an absolute path, assembled before chdir
from the checked kernel cwd result and the exact artifact ID. Stdio is opened
before child chdir. The parent never changes directory. Failed directory
creation or path/packet bounds stop the coordinator. Failed child chdir exits
97 before execution; failed exec exits 98; both differ from expected 0.

This is trusted fixture/evidence separation, not a sandbox, race-proof file
pinning, general process-tree limit or concurrent-store qualification. The
new positive regressions do not intentionally spawn effect children. Existing
standalone negative-control procedures are not added to the coordinator.

## Source verification performed

- Existing IDs 1–33 retained exactly; new entries and `authiso` calls match.
- Dispatch and dispatch-order committed gates match their documented source
  concatenations byte for byte; store and store-exact also match.
- Store-cleanup matches the documented five-prefix derivation of `store.om`
  followed by the current handwritten test verbatim. The temporary shell
  composer is removed; current derivation and replay instructions are in the
  authority README, not the historical preparation report.
- Global/text/parameter/local names obey the eight-byte bound, function names
  the 63-byte bound, parameters the six-argument bound, and blocks balance.
- New functions use scalar custom parameters, not global-array-to-view calls.
  Register capacity is inspected against the compiler's 16-slot profile,
  not proven by compilation.
- Existing compile/enable and expected-exit/reap failure paths are preserved.
  Child-only cwd and fail-before-exec ordering were independently reviewed
  against the existing host `cwd`/`chdir` implementation and test contracts.
- A host model of the reverse-copy and packet packing checked 40,950 combinations
  of cwd lengths 1–4095 and numbered artifact widths: 40,614 exact-target accepted
  cases and 336 capacity refusals. This is an algorithm/source check, not execution
  of the `.om` implementation. Independent review repeated a boundary model.
- Whitespace checks pass. Independent source review found no blocking defect.

The pinned crumb tool is the previously inspected host metadata utility from
`d0f0b95d344b4c6628c3d868346932fd247ce168`, not an Omega compiler. Structural
compile/verify passed (157 crumbs, only the root rewritten); the root metadata's evidence
count/root changes for this new report, without rewriting historical evidence.

## Not run and required replay

Omega compilation, all native positive/negative executions, the full coordinator
and independent Linux AArch64 replay remain **NOT_RUN**. The source-only posture adjustment removes the shell composer without changing
production sources. No bundled Omega native
binary was executed, and no compiler/toolchain or GPU dependency was installed.
Keep draft; source checks and metadata do not establish functional qualification.

On the approved Linux AArch64 qualification host, follow the existing
`seed/qualification/security673/README.md` reconstruction and full-gate recipe,
running each step only after its predecessor succeeds and using fresh scratch.
The full gate must exit 0 and include the three listed child results and retained
private fixture directories. The existing coordinator negative controls 0/1/2
must still fail with 99 (not a crash), each with fresh scratch. Replay each
regression's documented old-source control separately; failed compilation,
signal or timeout is INVALID, not a successful negative control.
