# Seeded compiler profiles for ecosystem milestone 660

This is a reproduction and interface handoff for architecture issue 303 and
Omega issues 660, 665 and 662. It does not integrate divergent compiler
branches, qualify a model, or close those issues. Existing compiler owner L408
retains the single write lane for `seed/omc/` and the seed rungs.

The machine-readable [profiles](profiles.json) freeze three distinct source and
executable identities. Consumers must select one whole profile. Combining the
host extension's features with the definition export's executable identity is
invalid: neither tested compiler contains both extensions.

| Profile | Native capability reproduced | Exact remaining boundary |
| --- | --- | --- |
| `baseline-29b12760` | Integer/boolean scalars, bounded arrays, direct/forward/mutual calls, standard input/output, checked arithmetic traps | Refuses the tested struct, argument and floating-point fixtures |
| `definitions-43f86be2` | Baseline plus structs and `function-definitions-v1` framed export | Refuses the argument and floating-point fixtures; export is metadata, not graph compilation |
| `host-f0499f41` | Baseline plus Linux ARM64 file, time, argument, local socket and child-process operations | Refuses struct and floating-point fixtures; process permissions are not a sandbox; no shared-memory thread or GPU qualification |

The host profile is already an implemented branch. An inventory must not call
files, clocks, arguments and child processes absent simply because the older
baseline lacks them. Application cutover, independent final-source review and
L408 integration remain separate gates. `execprog` uses a bounded packed
argument list, an empty environment and an exact executable path. It does not
invoke a shell or search PATH. All native host test processes were isolated;
no existing service, model residency or driver was changed.

## Reproduction

On Linux ARM64, use clean detached worktrees at the profile revisions. The
following are existing commands from those revisions, not newly written test
automation:

- omega-bridge `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9`:
  `quiet-build sh tools/bootstrap.sh --offline`.
- omega-mv `43f86be20f7257e02622f10a19abd2da2c98a290`:
  `quiet-build sh seed/omega-from-seed.sh`, then
  `quiet-build sh seed/omc/gate6.sh`.
- omega-mv `f0499f415571bdd7496b6de391096915e78f7556`:
  `quiet-build sh seed/host/check.sh`.

The first command emits `build/bootstrap/omc`. The latter two profiles emit
`seed/out/omc`. Verify their SHA-256 values against `profiles.json` before using
them. `quiet-build` is the existing Spark scheduling wrapper and can be
omitted on an independent machine without that coordinator. Source and build
tools still include shell and system utilities. Their presence is an open
first-party automation retirement task under issues 635/667, not an Omega-only
build claim. The bridge's source-only chain imports seed revision
`29b12760f4c962606411d6bf25032aedef063301`; it belongs to a separate repository.

The definition-export test entrypoints are the existing handwritten
`seed/omc/tests/definitions/{request,check,mutate,corpus}.om`. Compile them with
the definition profile. Feed one mode byte 0 through 12 to `request`; pass each
request to that compiler; concatenate responses in numerical order into
`check`. Expected result: `947 0`, exit 0. Feed each mode byte 0, 1, 2 followed
by the exact compiler source to `mutate`; compile each resulting source and
repeat the same requests and checker. Expected checker exit: 1 for all three,
with 446, 14 and 223 failed assertions respectively. This checks repository
binding, span endpoints and source digests with real compiled defects.

Capability inputs in `evidence/ECOSYSTEM660/capabilities/inputs/` are exact
copies of already committed `.om` fixtures. Their origins and expected exits
are in the profiles. Compile each fixture independently with each compiler.
Run only compiler exit-0 outputs. The argument fixture takes an empty argument,
`two words` and the UTF-8 bytes of `π`. A compiler refusal is never an executed
program PASS. Arithmetic trap exits are expected outcomes of their specific
negative fixtures, not successful applications.

## Verified results and limits

All three seed reconstructions pass. The definition profile passes 26 inherited
struct cases, 947 export assertions and all three compiled defect controls.
The host profile passes its full existing suite: nine positive groups,
41 negative controls and 70 original compiler fixtures with equal bytes,
status and diagnostics. The cross-profile matrix has 33 compile attempts and
23 executed programs, including 18 expected arithmetic/bounds trap exits.
Raw stdout, stderr, exit values, hashes and measured command costs are in
`evidence/ECOSYSTEM660/`. Independent system SHA-256 over seven raw request
source payloads reproduces the export test's expected source digests.

The inspected compiler executables have no ELF interpreter or dynamic-library
requirements. This observation does not establish runtime independence for
every child they could launch. Linux, hardware, firmware, shell orchestration
and trusted system comparison tools remain explicit dependencies.

Same-lineage stage equality proves reproducibility/consistency only. Diverse
double compilation under issue 650 remains NOT_RUN. This replay did not run the
historical full gate5/C-oracle performance campaign, GPU simulation or physical
devices, tensors, differentiation, a model, or a training workflow. No performance
improvement or quality claim is made. Coarse shared-host timings are bookkeeping;
energy and complete engineering/API cost are UNKNOWN.

## Next owned handoffs

1. L408 / issues 660 and 636: integrate the selected host/struct/export and
   bounded IR repairs under one reviewed compiler profile. Original dirty R6
   work remains preserved. This qualification lane does not change its source.
2. L408 / issues 663, 664 and 665: supply a qualified IEEE/numeric contract and
   current IR/backend interface before tensor/model consumers assume them.
   Integer-only probes do not establish FP/BF16/FP16 support.
3. Existing L386/CUDApp/L419/L495 owners / issues 638 and 661: supply the native
   device execution path and consumer provenance. This profile authorizes no
   GPU access or live cutover.
4. Independent reviewer and the existing L773 Merge Desk: review this exact
   evidence/profile addition and independently reproduce the source pins.
   Publication, judge dispatch and integration remain with the existing desk.

Search limitation: the configured XERJ map only exposed `setup-docs` (23
records). No project corpus was available. Claims here come from current Git
objects, canonical remote heads, original ledger rows and executed commands.
