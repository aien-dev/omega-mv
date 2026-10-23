# Bounded recursion in seeded Omega

This lesson adds the numbers1 through63 using the handwritten `accum` function
in `workloads.om`. Each call adds its current number to an accumulator and calls
itself with the next smaller number. The call with zero returns the accumulator.
There are64 calls, including that final call. The answer is2016.

`tutorial.om` first grants64 calls and64 live frames. It prints2016. It then
grants63 calls for the same calculation and prints80, the named fuel-exhaustion
status. A failure supplies no successful answer. The work and storage rules are
in [the proposed contract](../../../spec/universal/bounded-recursion-v2.md).

From a fresh checkout on Spark, run:

```sh
bash tests/universal/recursion/replay.sh
```

The existing source-aware builder reconstructs the pinned compiler from source
using the handwritten seed chain. It requires the already documented local
source dependency from `tools/source-aware/build`; `SA_SEED_REPO` can select a
clone containing that exact committed source. Nothing is silently downloaded.
The command writes only build scratch in this checkout. It requires Bash, Git,
Node and standard ELF tools as explicit build/test orchestration, plus the local
`quiet-build` scheduler. The executed workload is a static native AArch64 program
compiled from `.om`; it does not run through `.osc`, C, Rust or Python.

Success prints a JSON summary with status PASS, the actual case count and an
evidence directory. That directory contains the tutorial's actual output,
all command exits, source/tool/binary hashes, input/output streams, independent
reference results, compiled faulty controls, and full-process measurements.
Each run uses a new directory; an old successful result is never overwritten
or used as the result of a failed run. The tutorial binary is in that directory;
running it prints:

```text
2016
80
```

The same gate compares recursive and iterative tree/DAG evaluation, arithmetic
wraparound, missing-base recursion, exact and exceeded limits, argument order,
and a bounded mutual-recursion compiler probe. Mutual recursion is not admitted
by the proposed interchange profile. Measurements include startup, validation
and transport on a shared machine; they are not isolated function timings.
Faster or slower samples do not change which programs are accepted.

The old v1 source-aware codec still refuses recursive interchange. This lesson
qualifies specific handwritten source/native workload pairs, not a general
recursive importer or a translation proof. Cache/worklist integration, receipt
authentication, source-aware v2 implementation, independent final review and
the full Spark judge retain their separate gates.
