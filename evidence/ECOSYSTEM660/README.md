# Independent seed replay, 2026-10-23

Functional inputs are immutable upstream revisions; no compiler source was
changed. The profile contract and next owners are in
`seed/qualification/ecosystem660/`. This receipt is a bounded host result, not
whole-programme acceptance or diverse-double-compilation trust.

| Evidence | Meaning |
| --- | --- |
| `birth.*` | Fresh seed chain and definition-export compiler fixed point |
| `bridge.*`, `bridge-binaries.sha256` | Separate omega-bridge source-only baseline rebuild |
| `gate6.*` | 26 inherited struct gates |
| `host-check.*` | Separate native-host branch's complete existing qualification |
| `definitions/` | Replayed original 13 export cases, native 947-assertion oracle and three compiled compiler mutants |
| `capabilities/results.tsv` | Cross-profile compile and execution results; raw per-case errors/exits retained alongside |
| `capabilities/inputs/` | Exact pinned existing test source, retained as immutable evidence |
| `*.readelf.txt` | Actual executable segment/dependency observation |
| `seed-{before,after,final}.sha256` | Definition-profile tracked seed bytes before and after tests |
| `consumed.sha256` | Exact separately consumed source, binary and host contract identities |
| `remote-refs.txt` | Canonical branch observations at qualification |
| `tools.txt`, `host.txt` | Actual host and external tool identity observations |

Native compiler mutants are retained as `.om` sources under `definitions/`.
Their compiled ELF identities are recorded in `definitions/binaries.sha256`;
executables remain untracked build artifacts. These sources deliberately
implement defects and must not be selected as consumer compilers. Their
successfully compiled, wrong-output behavior is rejected by the unmodified
native oracle. This is stronger than counting a compiler refusal as a caught
semantic mutation.

Original source inputs for capability tests are pinned in `profiles.json`.
No newly authored foreign-language test runner was added. The existing shell
test entrypoints and tool invocations are disclosed in the profile README.
Per-file compile/run output and status are retained even for expected failures.
Only successful compiler outputs were executed. `struct` correctly returns
20 in its positive fixture; this exit is unrelated to compiler error20.

The source-only bridge and native-host worktrees remained at their pinned
revisions with no tracked modifications. The definition worktree contains only
this additive profile/evidence packet. Existing immutable evidence was not
rewritten. Host tests used their own temporary files and private child
processes; loopback TCP ports were selected by the kernel.

Costs are coarse `/usr/bin/time` observations on a shared host, with scheduler
wrapper overhead included in complete rebuild samples. They are not a
performance qualification. Definition-control orchestration totals, complete
engineering/API costs and energy are UNKNOWN. No model/config/checkpoint was
consumed. GPU, simulator, full gate5/C-oracle, DDC and end-to-end model gates
are NOT_RUN. Final-source independent review of this profile packet is pending.
