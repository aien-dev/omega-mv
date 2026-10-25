# Offline append safety game (#760)

SC1-G-v1 is a finite experiment under the existing seed qualification area. It adds no runtime scheduler, authority, store or general verifier. `MODEL.md` and `fixtures.tsv` froze the public transition model and behavioral expectations before implementation; the parent reviewed its layout. The requested hidden-occupancy product was frozen separately before its implementation. This directory does not close #673 or omnet#3.

## Results and limits

Native Linux AArch64 execution with the pinned seed compiler produced:

- Enumerated universe2593 states, including BAD;1040 locally invariant states;17082 labeled state/action/disturbance edges. These are not deduplicated source/destination edges or reachable-only counts.
- Greatest fixed point1000 states, including initial0. Synchronous solver and independent reverse-order losing-attractor checker both took3 passes, including the final unchanged pass.
-1040 paired public-state representatives and17082 paired transition checks. Every actual action/disturbance uses one common public action, equal enabled masks, equal observations and equal full public successor projection. Hidden occupancy1/2 is inside a fixed public reservation2. No secret affects protected actions or transitions by construction; exhaustive checking is finite relational evidence for that explicit extension only. The shared candidate mask and cost selector depend only on public state. It is not machine leakage, complete secret-closure, timing or scheduler qualification.
- Six solver controls compiled and executed; the checker normally exited71..76 at QUANTIFIER, OMITTED_DISTURBANCE, STALE_EPOCH, UNCERTAIN_REFUND, UNSAFE_OPTIMIZER and EARLY_STOP respectively. The secret-dependent admission control is compiled into checker mode8 and normally exits78 at PAIRED_PRODUCT. A build failure, signal, unrelated failure or timeout is never a valid control result.

`evidence/native-v2/candidate0.bin` is the frozen complete result corpus, SHA256 `310c998da4a45a737871e5c31bc80ed9f2fd2a395f54dbb6ea6a03b8fa500523`. Each negative corpus and raw log is retained. `native-v1` preserves a failed paired-label initialization attempt; its paired negative result was INVALID because its positive baseline failed too. Earlier compile errors (unsupported bool-to-byte conversion and register pressure in the measurement conductor) were repaired and are not counted as passing controls.

The locally invariant losing state15 has two legal split-fence proposals. Epoch-only action6 loses to substitute4; object-only action7 loses to revoke3; both lead to BAD2592. For each disturbance an action could be chosen after seeing it, which is exactly the prohibited quantifier swap. State16 advances to15 and explains why one synchronous elimination pass is insufficient. These are explicitly defective candidate phases, not claims about existing service states.

The native serial trace queues both clients, reserves client0, prepares/writes/renames/settles/acknowledges, then does client1. At state20 eager client1 reservation is unavailable because4>3. At state32, after client0 retains only its durable unit, client1's complete2-unit reservation fits3. Final state266 holds both acknowledged records, charge2. A fixed eager-policy refusal is not general schedule infeasibility.

The generic epoch transition is not TKA parent-state/fork-choice authorization. It invalidates a prepared generation; a new prepare assumes current authorization has been established. The model's fixed operation identities/idempotent retry, capacity ledger, atomic fencing and authoritative cleanup/recovery are unimplemented correspondence obligations for the real consumer. Rename enters uncertain phase11 (linearization); settle establishes directory-sync acknowledgement eligibility. No power-loss or writeback-EIO recovery claim follows.

The run was designed against SC1 draft `8bc52be7a480e279fb77673648a2f7775ba6d4841a8b1e6c427531b25d4d1860`. The later reviewed SC1 revision2 (`8dab6ec636a27711c00245936c4d3250309f46e6289d4bace7f37713916ad9d2`) explicitly separates those real-consumer gaps. These receipts retain the draft identity and do not automatically qualify revision2. Parent integration/review owns that disposition.

## Implementation and checking

`solve.om` packs states arithmetically, materializes the transition table and synchronously eliminates states. `check.om` separately decodes fields, uses an independent phase-action table and reverse-ordered in-place losing attractor, and regenerates every submitted transition. It checks initial inclusion and the exact full safe-action relation, not just one strategy. Thus maximality is only for this finite model; an arbitrary safe subset would fail exactness. Only binary transport, numeric formatting and sample sorting in `io.om` are shared. Both implementations share the seed compiler lineage; this is not diverse compilation or a formal proof.

The certificate is little-endian u32: magic1396912455, version1, state-count2593, then for each state an enabled-action mask, a retained-safe-action mask and nine successor IDs per enabled action in ascending action order. No trailing bytes are accepted. All nine disturbances are explicit; empty action sets refuse, the checker exercises empty/out-of-bound disturbance counts, and quiescence has an explicit stutter. Compiler bound traps, malformed input and I/O failures invalidate the run.

Synthetic action costs rank only independently safe mask bits. Evidence identity0 or2 refuses ranking;1 names the declared synthetic table. Unsafe split has cost0 but is never admitted. This is not #583 learned cost data, an optimal runtime schedule or a performance prediction. #647's general native analysis seam and #623 concrete compiled-consumer correspondence remain BLOCKED on their existing owners.

## Measurements

All timings are host MEASURED nanoseconds, not cost-model estimates.101 raw samples per population, nearest-rank percentiles (indices50/95/99). No quiet CPU isolation or held-out calibration was claimed.

| Population | p50 ns | p95 ns | p99 ns |
|---|---:|---:|---:|
| Construct transitions + solve + construct certificate, in-process |1036298|1042826|1068459|
| Read corpus + independently regenerate/check + losing attractor + paired product |4327434|7528104|7617658|
| Full two-child solve/write/read/check lifecycle |11819664|13044092|13973844|

Measured throughput over the101 native intervals was957.390438 solver iterations/s,217.541600 complete checker iterations/s and91.620047 full lifecycle iterations/s. These are101 divided by the sum of that population's recorded intervals, not reciprocals of a tail percentile and not service-request throughput.

One cold demonstration measured construction2110709ns, solve+certificate331075ns, and complete checker4493995ns. Declared global array storage including transport and samples is1711182 bytes for solver and18125 for checker, excluding code, text, stack and alignment. GNU time observed peak RSS1120KiB and192KiB respectively; sparse untouched global pages explain why RSS can be below reserved array bytes. Full lifecycle peak RSS was1120KiB.

The fixed `roundtrip.om` conductor reuses gate669's fork/exec/wait pattern, checks both child exits and signals, enforces30-second child deadlines, and includes process creation, regular-file corpus I/O and1ms wait-poll overhead in each sample. It is a measurement instrument, not another qualification framework. It suppresses child logs during101 repetitions; the separate positive/control logs retain diagnostics. In-process solver samples exclude certificate output; checker samples include regular-file input. Neither measurement includes service integration, real keys, fsync/power-loss experiments, GPU execution or a live workload. Raw negative/no-win timing is retained rather than presented as improvement.

## Exact replay

From a clean checkout at the source commit in the content-addressed receipt, Linux AArch64, run each build command only after the preceding command succeeds. The existing bootstrap shell and coreutils are retained tooling; all new qualification and measurement logic is `.om`.

```sh
nice -n 19 sh seed/omega-from-seed.sh
sha256sum seed/out/omc
# REQUIRED: 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
mkdir -p build/security760-native
cat seed/qualification/security673/game/io.om seed/qualification/security673/game/solve.om > build/security760-native/solve.om
nice -n 19 seed/out/omc < build/security760-native/solve.om > build/security760-native/solve
chmod +x build/security760-native/solve
cat seed/qualification/security673/game/io.om seed/qualification/security673/game/check.om > build/security760-native/check.om
nice -n 19 seed/out/omc < build/security760-native/check.om > build/security760-native/check
chmod +x build/security760-native/check
cat seed/qualification/security673/game/io.om seed/qualification/security673/game/roundtrip.om > build/security760-native/roundtrip.om
nice -n 19 seed/out/omc < build/security760-native/roundtrip.om > build/security760-native/roundtrip
chmod +x build/security760-native/roundtrip
nice -n 19 /usr/bin/time -v build/security760-native/solve 0 > build/security760-native/candidate.bin
nice -n 19 /usr/bin/time -v build/security760-native/check < build/security760-native/candidate.bin
# Both exit0. Compare the corpus hash above.
```

For each control N=1,2,3,4,5,6 invoke these literal commands with N replaced by that single digit. The solver must exit0; the checker must normally exit70+N and print the named assertion listed above. A command interpreter loop is not the checker.

```sh
nice -n 19 build/security760-native/solve N > build/security760-native/controlN.bin
nice -n 19 build/security760-native/check < build/security760-native/controlN.bin
nice -n 19 build/security760-native/check 8 < build/security760-native/candidate.bin
# The last command alone must exit78 at PAIRED_PRODUCT.
nice -n 19 /usr/bin/time -v build/security760-native/solve 7 > build/security760-native/bench.bin
nice -n 19 /usr/bin/time -v build/security760-native/check 7 < build/security760-native/candidate.bin
nice -n 19 /usr/bin/time -v build/security760-native/roundtrip
```

Benchmark commands must exit0. The roundtrip spool uses exclusive creation and refuses an existing `build/security760-native/roundtrip.bin`; replay in a fresh scratch checkout rather than overwriting evidence. Run the existing repository gates and independent final-source review through the main integration owner. This lane exercised the seed bootstrap and focused native game checks only; it did not run unrelated release gates or claim final independent review.
