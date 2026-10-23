# L775 bounded native replay

Tested source: `84d03c5523f39ca1045d1c313c5f62597779462b`.
Compiler, sources and executable SHA256 values are in `source-artifacts.sha256`.
Compiler source and lineage are specified in `docs/learning/benchmark-v1.md`.
L776 source `f1ca34e99747fd03371f708d24529126af79b9bb` was separately extracted
from Git, recompiled with the same immutable compiler and executed here.

| Actual command result | Outcome |
|---|---|
| `core-final.compile.exit`, `core-final.run.exit` | 0, 0; stdout reports 185 checks |
| `profile-positive` | compiler 0, native execution 0 |
| `profile-trap-overflow` | compiler 0, native exit 101 |
| `profile-trap-div` | compiler 0, native exit 102 |
| `profile-trap-bounds` | compiler 0, native exit 103 |
| `profile-trap-loop` | compiler 0, native exit 104 |
| `profile-trap-conversion` | compiler 0, native exit 105 |
| `profile-trap-shift` | compiler 0, native exit 107 |
| `profile-refuse-type` | compiler refusal 7, no executable run |
| `profile-refuse-float` | compiler refusal 3, no executable run |
| `mutant-pins2` | compiler 0; same native component test fails at exit 120 |
| `mutant-scope` | compiler 0; same native component test fails at exit 48 |
| `peer-oracle`, `peer-observable` | compile/run 0, 128 identical bytes |
| `peer-badvalue` | compile/run 0, 128 bytes with a wrong saturating value |
| `observe-good`, `observe-bad` | native observation consumer exits 0 and 7 |
| `observe-empty`, `observe-short`, `observe-long` | malformed input refused at exits 40, 41, 42 |

The two bypass sources are handwritten mutations of the exact component
source: `mutant-pins.om` accepts every supplied identity; `mutant-scope.om`
bypasses public partition/visibility checks. Both compile successfully and are
then caught by the unchanged component assertions. `mutant-pins` attempt 1
incorrectly left unreachable code, giving compiler refusal 17; its source and
diagnostic are preserved and it is not counted as a successful mutant control.

The abandoned `maxattempts` identifier refusal is retained in the parent
evidence directory. The first resumed compilation gave register-pool refusal
12 at line 41, the second gave the same refusal at line 284, and the third
compiled successfully. Splitting record/pair validation and elapsed-cost
expressions resolved those failures without changing the compiler. The first
native execution failed at check one because the old fixture reused a numeric
family across train/dev; the new family check correctly caught it. The corrected
fixture passes. Original sources are retained as `core-before.om.txt` and
`test-before.om.txt`. Later component runs grow from the earlier passing set
to the final 185 checks. Intermediate diagnostics are historical records;
not every intermediate transient source has a separate source pin.

## Reproduction boundary

Use the source-only omega-bridge revision above and its existing bootstrap
command. Confirm compiler SHA256 before any compile. Existing development
commands concatenate the library with `core-test.om` or `observe-pair.om` into
one translation unit, feed it to the pinned compiler with `quiet-build`, verify
the output using `readelf -h`, mark only that owned ELF executable and run with
`timeout 10`. The exact source and binary hashes identify the commands' inputs
and outputs. `commands.json` records the command forms; there is no new shell
or foreign-language runner. All new program/test logic is `.om`.

Observation tests concatenate the public independent oracle's 128 bytes with
the observed program's 128 bytes on stdin. The mutated program itself exits
zero; rejection comes from independent byte comparison. It is not a trap-based
substitute for a wrong-value control. Empty, truncated and extra-byte inputs
are separately refused before producing a success status.

`*.cost` records external command wall/user/system seconds and maximum RSS
where measured. These are coarse shared-host bookkeeping, not timing-qualified
benchmarks. Repair/abandoned attempts without instrumentation have UNKNOWN
cost; energy and model costs were not measured. No model, tokenizer, protected
dataset, final exam or GPU was used. This slice does not supply the native
runner, authentication, protected partition custody or matched model baselines.
Full #653, #652 and later ordered work remain open. Independent full source
review is pending through the existing owner; no PR merge or release is claimed.
