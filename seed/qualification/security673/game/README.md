# SC1-G-v3 offline safety game (#760)

Current experiment: `MODEL-v3.md`, SHA256 `fa062dba15b5b7cccacfa4a5ea4b670d15b9a8c9e764b68b6e53b70dabd0eb19`. The parent approved and [published the freeze](https://github.com/aien-dev/omega-mv/issues/760#issuecomment-6402892635) at commit028fc3afb before the source repair. Final SC1 revision2 is `ced61decef1ff4efbf5bc81598db648aa810be434e373e9bc1563dbcb4523496`. This is a bounded offline experiment, not a scheduler, authority, store, general verifier or omnet policy implementation.

## Review repair and history

Independent review found the prior experiment incorrect: zero-cost split could refuse and win ranking in120 changed-generation states; stale rename was checked only as transition conformance; fixed certificate fingerprints assigned control labels; the paired product did not consume the proposed certificate; and variants shared a runtime-selected binary. Those claims are superseded, not silently qualified by the new run.

`MODEL.md`, `fixtures.tsv`, `evidence/native-v1` and `native-v2` are historical. The old model was amended after a failed run without a version bump. The amended bytes remain in commit a485cfa1c633d2cc5fd51df24c73aa8c059b8fbc. The original model was recovered from those bytes by removing exactly the transcript-recorded hidden-label paragraph, then verified against the recorded full SHA256 `f5a038d26bfa65b519038a5882cece3b7b7612a0fbf83017a5e5b1d45afdd057`; it is added as `native-v1/MODEL.original.md`. The old draft/revision hash citations and missing external pre-implementation publication are historical defects. Prior compilation errors were not retained as raw files; they were not counted as valid controls. No missing original is being reconstructed from guesses.

V3 repair source commit: `8554be4ddb0b436260cc7e051e156f1893a3e7b9`. All10 v3 executable builds succeeded on their first recorded attempt. Concatenated source snapshots, compiler logs, raw outputs and exact command/input bindings are retained under `evidence/native-v3`.

## Native finite results

Pinned compiler `734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61`; Linux AArch64 native execution, no emulator/GPU/service execution.

| Result | Observed |
|---|---:|
| Enumerated universe, including BAD |2593 states|
| Locally invariant states |1040|
| Labeled state/action/disturbance edges |16002|
| Greatest fixed point |1000 states|
| Solver/checker passes, including final unchanged pass |3 / 3|
| Distinct hidden1/2 public-state representatives |840|
| Distinct-hidden certified transition pairs |12600|
| Identical hidden0/0 representatives |200|
| Identical-zero certified transition pairs |2484|

These are not reachable-only or deduplicated source/destination counts. The840 distinct-hidden representatives include40 losing states whose proposed safe masks are empty; enabled-mask comparison still covers them. Paired transitions enumerate proposed safe actions, not every rejected action. All16002 raw implementation transitions separately pass monitor and conformance checks.

Frozen positive corpus: `candidate0.bin`,95140 bytes, SHA256 `0af558d845653cb5ed94baa94673adc119c00defe118d36b8d63e2382b66e855`.

The solver packs states and performs synchronous elimination. The checker independently decodes fields, uses a separate phase-action table and reverse-order losing attractor. Only transport, numeric output and sample sorting are shared. Both share the compiler lineage. Initial inclusion, every submitted action against all nine disturbances, exact maximal safe-action relation, every winning state's selected action and every step of the serial witness are checked. `fixtures-v3.om` is compiled into the checker and consumed by these assertions; historical fixtures.tsv is not an active input.

The rename monitor is independent of the transition implementation: it reconstructs current generation after interference from original source operands and observes writing-to-uncertain as a rename. A stale rename maps to BAD even when the raw stock invariant holds. Ordinary mismatched commit must refuse unchanged. Uncertain-to-refunded also maps to BAD. This is a transition-event monitor, not a state-only freshness proof or observation of an unmodeled real external effect. Raw transition conformance is a separate obligation.

## Source-fixed controls and actual rejection witnesses

Each `faultN.om` fixes the fault before compilation, yielding distinct concatenated source and executable hashes. The positive binary cannot select a fault through argv. Mode7 is measurement only. Checker8 is a separate binary, not a mode of checker0.

| Control | Intended assertion / normal exit | Actual witness |
|---|---|---|
| G2 swapped quantifiers |CLOSURE, QUANTIFIER_CLOSURE /71|state15, action6, substitute4 -> BAD2592|
| G3 omitted substitute |CLOSURE, OMITTED_DISTURBANCE_CLOSURE /72|state15, action6, substitute4 -> BAD2592|
| G4 stale rename |FRESHNESS_AT_RENAME /73|state7, action6, revoke3 -> raw335, monitored BAD2592|
| G5 uncertain refund |UNCERTAIN_REFUND /74|state11, action7, partialwrite6 -> raw17, monitored BAD2592|
| G6 unsafe cheapest selector |SELECTED_NOT_SAFE /75|state3 selects9; certified mask40 excludes it|
| G7 early iteration stop |CLOSURE, EARLY_STOP_CLOSURE /76|state16, action9, disturbance0 -> excluded15|
| G11 secret-dependent admission |PAIRED_PRODUCT /78|state20: hidden1 enabled mask3096 versus hidden2 mask2072|

The checker has no fixed state15/16/3 rejection prechecks. It prints witnesses found by real closure, monitor or selection checks. The untrusted wire experiment label annotates a demonstrated closure failure; it is not a diagnosis inferred from a fingerprint. G2 and G3 intentionally share the same closure witness. Build errors, signals, timeouts and arbitrary nonzero exits are INVALID, not passing controls.

The actual optimizer mutation changes its candidate filter from safe bits to enabled bits. Every winning state is ranked and checked, including changed-generation states. Split is now enabled at selection only for current epoch/object0/0, and is never a certified choice. Missing/stale cost identities refuse. Costs are declared SYNTHETIC, not #583 runtime estimates or a performance/progress prediction.

Local-valid state15 is losing: epoch-only action6 loses to substitute4, object-only action7 to revoke3. State16 exposes premature fixed-point termination. The certified serial trace reaches state20 where eager client1 reservation would require4>3; after client0 settles/acknowledges it reaches32, where the second full reservation fits3; final266 retains both records with charge2. Each action and successor in this trace is checked against the submitted certificate.

## Paired scope and correspondence limits

The product consumes the proposed safe masks and public selection. Enabled masks must agree across the pair; the safe mask is contained in both, one public selected action is shared, and each proposed action uses the same disturbance. Public observations and successor projections must match; successors remain in the proposed region with well-formed hidden labels. The source explicitly keeps secret labels out of protected transitions. This is by-construction equality plus exhaustive finite checking, not machine leakage, timing, complete secret closure or actual scheduler qualification. The hidden-dependent admission baseline fails against the positive corpus.

Generic epoch invalidation is NOT TKA parent-state/fork-choice authorization. Fixed operation identities, idempotent retry, complete atomic reservations, cleanup, recovery and fencing remain model assumptions. Rename is linearization; settle represents directory-sync acknowledgement eligibility, not a power-loss/writeback-EIO guarantee. Formal proof, diverse compilation and emulated execution are not claimed. Actual omnet correspondence, general #647 integration, #623 consumer translation and runtime adoption remain BLOCKED with their existing owners.

## Measured overhead

101 native samples per population; nearest-rank indices50/95/99. Host measurements only, no isolated CPU or held-out calibration claim.

| Population |p50 ns|p95 ns|p99 ns|Measured iterations/s|
|---|---:|---:|---:|---:|
|Construct + monitored solve + certificate + selection|1238365|1253597|1263309|804.123814|
|Read + monitor/conformance + attractor + closure/exactness + selection + product|4882131|4924708|4985604|204.446558|
|Full two-child solver/write/read/check lifecycle|9919016|14000035|14094804|91.725978|

Throughput is101 divided by the sum of the population's raw intervals, not a percentile reciprocal or service throughput. One cold run measured construction1515344ns, solve/certificate/selection392452ns and checker8589916ns. Declared global array storage, including transport and samples:1721554 solver bytes and28517 checker bytes, excluding text/code/stack/alignment. Raw GNU time logs retain RSS and process resource measurements (positive solver1120KiB, checker192KiB).

`roundtrip.om` reuses gate669's fork/exec/wait pattern for two fixed children, checks exits/signals and30-second deadlines, and includes process creation, corpus file I/O and1ms wait-poll overhead. Child logs are suppressed only for101 measurement repetitions; separate positive/control runs preserve diagnostics. The spool is exclusively created. No failed/no-win result or earlier raw evidence was edited.

## Exact replay

Run from a fresh Linux AArch64 checkout. Existing bootstrap/coreutils are retained tooling; all new logic is .om. Stop on any build failure; do not chmod or execute partial compiler output. The command/input manifest in native-v3 binds each checker invocation to its exact stdin corpus SHA256, argv, executable and outputs, and hashes this README.

```sh
nice -n 19 sh seed/omega-from-seed.sh
sha256sum seed/out/omc
# REQUIRED compiler hash:734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
mkdir -p build
mkdir build/security760-v3
G=seed/qualification/security673/game
cat "$G/io.om" "$G/fault0.om" "$G/solve.om" > build/security760-v3/solve0.om
cat "$G/io.om" "$G/fault1.om" "$G/solve.om" > build/security760-v3/solve1.om
cat "$G/io.om" "$G/fault2.om" "$G/solve.om" > build/security760-v3/solve2.om
cat "$G/io.om" "$G/fault3.om" "$G/solve.om" > build/security760-v3/solve3.om
cat "$G/io.om" "$G/fault4.om" "$G/solve.om" > build/security760-v3/solve4.om
cat "$G/io.om" "$G/fault5.om" "$G/solve.om" > build/security760-v3/solve5.om
cat "$G/io.om" "$G/fault6.om" "$G/solve.om" > build/security760-v3/solve6.om
cat "$G/io.om" "$G/fault0.om" "$G/fixtures-v3.om" "$G/check.om" > build/security760-v3/check0.om
cat "$G/io.om" "$G/fault8.om" "$G/fixtures-v3.om" "$G/check.om" > build/security760-v3/check8.om
cat "$G/io.om" "$G/roundtrip.om" > build/security760-v3/roundtrip.om
nice -n 19 seed/out/omc < build/security760-v3/solve0.om > build/security760-v3/solve0 && chmod +x build/security760-v3/solve0
nice -n 19 seed/out/omc < build/security760-v3/solve1.om > build/security760-v3/solve1 && chmod +x build/security760-v3/solve1
nice -n 19 seed/out/omc < build/security760-v3/solve2.om > build/security760-v3/solve2 && chmod +x build/security760-v3/solve2
nice -n 19 seed/out/omc < build/security760-v3/solve3.om > build/security760-v3/solve3 && chmod +x build/security760-v3/solve3
nice -n 19 seed/out/omc < build/security760-v3/solve4.om > build/security760-v3/solve4 && chmod +x build/security760-v3/solve4
nice -n 19 seed/out/omc < build/security760-v3/solve5.om > build/security760-v3/solve5 && chmod +x build/security760-v3/solve5
nice -n 19 seed/out/omc < build/security760-v3/solve6.om > build/security760-v3/solve6 && chmod +x build/security760-v3/solve6
nice -n 19 seed/out/omc < build/security760-v3/check0.om > build/security760-v3/check0 && chmod +x build/security760-v3/check0
nice -n 19 seed/out/omc < build/security760-v3/check8.om > build/security760-v3/check8 && chmod +x build/security760-v3/check8
nice -n 19 seed/out/omc < build/security760-v3/roundtrip.om > build/security760-v3/roundtrip && chmod +x build/security760-v3/roundtrip
nice -n 19 build/security760-v3/solve0 > build/security760-v3/candidate0.bin
nice -n 19 build/security760-v3/check0 < build/security760-v3/candidate0.bin
```

All preceding native exits must be0. For the following controls, each solver must exit0 and each checker must normally fail at the specified assertion. Do not use a shell's generic nonzero exit as qualification.

```sh
nice -n 19 build/security760-v3/solve1 > build/security760-v3/candidate1.bin
nice -n 19 build/security760-v3/check0 < build/security760-v3/candidate1.bin
# expected71 QUANTIFIER_CLOSURE after actual CLOSURE witness
nice -n 19 build/security760-v3/solve2 > build/security760-v3/candidate2.bin
nice -n 19 build/security760-v3/check0 < build/security760-v3/candidate2.bin
# expected72 OMITTED_DISTURBANCE_CLOSURE after actual CLOSURE witness
nice -n 19 build/security760-v3/solve3 > build/security760-v3/candidate3.bin
nice -n 19 build/security760-v3/check0 < build/security760-v3/candidate3.bin
# expected73 FRESHNESS_AT_RENAME
nice -n 19 build/security760-v3/solve4 > build/security760-v3/candidate4.bin
nice -n 19 build/security760-v3/check0 < build/security760-v3/candidate4.bin
# expected74 UNCERTAIN_REFUND
nice -n 19 build/security760-v3/solve5 > build/security760-v3/candidate5.bin
nice -n 19 build/security760-v3/check0 < build/security760-v3/candidate5.bin
# expected75 SELECTED_NOT_SAFE
nice -n 19 build/security760-v3/solve6 > build/security760-v3/candidate6.bin
nice -n 19 build/security760-v3/check0 < build/security760-v3/candidate6.bin
# expected76 EARLY_STOP_CLOSURE after actual CLOSURE witness
nice -n 19 build/security760-v3/check8 < build/security760-v3/candidate0.bin
# expected78 PAIRED_PRODUCT
nice -n 19 /usr/bin/time -v build/security760-v3/solve0 7 > build/security760-v3/bench.bin
nice -n 19 /usr/bin/time -v build/security760-v3/check0 7 < build/security760-v3/candidate0.bin
nice -n 19 /usr/bin/time -v build/security760-v3/roundtrip
# all three measurement commands expected0
```

Use a new scratch checkout for roundtrip replay: an existing `roundtrip.bin` refuses exclusive creation. No release gates, live model/GPU workload or service deployment were exercised by this lane.

## Independent final review and clean replay

GameReview rechecked source46c0cd2e200cf136e5f8a2715bc6625e0de0758b and found no remaining actionable defects within the finite/offline scope. `clean-replay.json` records a separate fresh-checkout bootstrap, all ten successful builds, the exact positive corpus hash above, normal controls71/72/73/74/75/76/78 and all three measurements. The initial checker benchmark invocation incorrectly supplied a pipe; exit90 is retained as INVALID setup. The documented seekable-file invocation then completed0. No code was changed to obtain that result.

The native-v3 receipt's README hash refers to its historical46c0cd2 documentation bytes. This final review/replay section is later documentation, not a rebind of those old measurements. Current clean-run raw samples are separate; host-load variation is not a measured regression or a calibrated tail guarantee.
