# omega-host

OSH, the Omega shell and its Linux host adapter. The shell core (lexer, parser, expander) is written in OSC,
Omega's systems language, and runs through Omega's checked entries; the host adapter in C runs the commands it
parses on Linux, each one checked against Omega's host capability root first and recorded in a durable
intent/outcome journal (the ABI in aien-architecture#158 and aien-protocols OSH_PLATFORM_ABI.md).

It moved out of Omega with its history (ADR 0039: one thing per repo). It builds inside the exact Omega
commit pinned in `omega.lock` (an Omega without these files, so nothing is borrowed from Omega's copy): `make test` checks that commit out under `build/omega`, lays this repo's
files over it at the same paths, and runs every OSH test there, so the results are the ones Omega
produced when OSH lived inside it.

    make test     # lexer, parser, expander, end-to-end, native == interpreter traces, host, capabilities, journal

| Folder | What it is |
|---|---|
| `src/osh/` | the OSC sources (lexer, parser, expander) and the host side in C (`src/osh/host/`) |
| `tests/osh/` | unit tests, end-to-end scripts and the committed traces every run must match |
| `mk/osh.mk`, `mk/osh-host.mk` | the build rules, unchanged from Omega |
| `omega.lock`, `tools/omega_tree.sh` | which Omega this builds against, and how that tree is made |

Licence: AGPL-3.0, the same as Omega.

## Follow-on qualification

The retained OSC shell core and C Linux adapter are compatibility scaffolding, not the seeded Omega runtime. This candidate carries bounded reviewed host fixes; whole Bash/native AIENOS/hardware equivalence is not implied.

`make test` preserves all original targets and includes the differential reader, signal ownership, atomic prompt/PTY and numeric/argument boundaries. `make qualify` additionally executes the compiled fault controls, preserving exact expected failure sets. `make test-plan` checks actual target coverage and refuses omissions, undefined targets and duplicates. Linux GNU make, a C compiler, Git, Bash, Node.js and Python3 are host test dependencies; no model/server is loaded. Use a private caller-owned output tree.

Historical control functions and entry failures are included as pinned fixtures so a fresh module/consumer checkout does not depend on unrelated parent-branch Git objects. Fixture hashes and original references are in `evidence/L570-QUALIFICATION-WIRING/baseline-provenance.json`. Original assertions/corpora and fault verdicts are unchanged. This is source/runner preparation, NOT a passing module qualification receipt until the exact final candidate and its consumer replay actually finish.
