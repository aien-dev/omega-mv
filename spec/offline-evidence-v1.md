# Offline evidence schema v1 (OEV1) for the resource-DAG cost simulator

Status: FROZEN 2026-10-19 (omega-mv#583, implement item 1). Consumers: omega-mv#583
(`tools/dagsim`), omega-mv#584 (candidate screening), aien-architecture#246 (lifecycle
ledgers). A change to a frozen field is a v2, never an edit of v1.

Purpose: one text file describes a machine, the build and calibration it was measured
with, the resources, an operation DAG with demands, buffer lifetimes, dependencies,
synchronization and the energy instrument, so that an offline evaluator (no GPU, no
execution) can compose existing per-operation estimates into whole-DAG costs and bounds.
The file never carries a number without saying where it came from.

## File form

Plain text, UTF-8, one record per line, fields separated by whitespace, `key=value`
pairs after the record keyword, `#` to end of line is a comment. Unknown keywords are
errors (a parser that silently ignores a field would let a fabricated or misspelled
number through). Line numbers are reported on every error. No value may contain
whitespace. Maximum 256 operations, 32 resources, 256 buffers, 1024 dependencies
(static limits; a larger DAG is refused with `REFUSED_UNSUPPORTED`, not truncated).

Units are fixed by the schema and never per record: time `ns`, bytes `B`,
bandwidth `B/s`, power `W`, energy `J`, capacity `B`. Dates are `YYYY-MM-DD`.
Integers are decimal; reals accept `1.5e9`.

## Provenance (mandatory wherever a number appears)

`prov=` takes exactly one of:

| value | meaning |
|---|---|
| `specified` | a vendor or design ceiling (datasheet). Never attainable by itself. |
| `measured` | read from an instrument on the named machine; `src=` names the receipt. |
| `fitted` | produced by a model fitted on measurements (e.g. `rx_costmodel`); `src=` names the model blob or the fit receipt. |
| `synthetic` | made up for a sensitivity study; results that depend on it are labelled synthetic and never ranked as real. |
| `unknown` | nobody knows. The evaluator refuses to turn this into a number. |

`lo=` and `hi=` give the interval that the source supports (for `measured`: the repeat
spread of the receipt; for `fitted`: the model's predictive interval). Missing `lo`/`hi`
on a `measured` or `fitted` value means "no interval known" and the evaluator reports
the result without interval coverage.

## Records

```
oev version=1
machine id=<host> soc=<name> cores=<n> mem_bytes=<B> prov=specified src=<where>
build repo=<name> sha=<40 hex> [physics=<sha>]
calibration id=<name> taken=<YYYY-MM-DD> valid_days=<n> load1=<x> src=<receipt dir or file> [model=<path to rx_costmodel blob>]
resource id=<name> kind=compute slots=<n> prov=<p> src=<s>
resource id=<name> kind=bandwidth cap=<B/s> [lo=<B/s> hi=<B/s>] prov=<p> src=<s>
resource id=<name> kind=capacity cap=<B> prov=<p> src=<s>
resource id=<name> kind=launch prov=<p> src=<s>
op id=<name> kind=<compute|copy|matvec|barrier|storage|persist> [engine=<compute res>] [t_ns=<x> lo=<x> hi=<x> prov=<p> src=<s>] [bytes=<B> bw=<bandwidth res>] [launch_ns=<x> launcher=<launch res>] [M=<n> N=<n> core=<0|1|2> arm=<n>]
dep <from op> <to op>
buffer id=<name> bytes=<B> cap=<capacity res> [first=<op>] [last=<op>]
sync id=<name> ops=<op,op,...>
energy instrument=<none|name> [boundary=<wall|gpu_module|soc>] [joules=<J> prov=measured src=<receipt>] [power_w=<W> prov=synthetic]
removed op=<id> reason=<eliminated|reused|unchanged_dep|deferred|moved|shared|lookup> [ref=<op or result identity>] [proof=<src>]
```

Semantics the evaluator is allowed to rely on:

- `machine`, `build`, `calibration` appear once. Every `measured` or `fitted` value is
  valid only inside the calibration's validity envelope: the evaluator takes an
  `asof` date and refuses (`REFUSED_STALE`) when `asof > taken + valid_days`, and refuses
  (`REFUSED_NO_CALIBRATION`) when a measured or fitted value appears without a
  `calibration` record. `load1` is the host load during the measurement; it travels with
  every result so a loaded-box number is never quoted as a quiet-box number.
- `resource kind=compute slots=n`: n operations may run at once on this engine. A
  compute resource is a CPU core pool or the GPU; the schema does not model SM-level
  occupancy.
- `resource kind=bandwidth cap=B`: a shared fluid pipe. k operations streaming at once
  each get `cap/k` (processor sharing). This is the only contention rule in v1; it is
  what makes "sum of concurrent full-bandwidth claims" impossible by construction.
- `resource kind=capacity`: a byte ledger. CPU and GPU unified memory are ONE capacity
  resource on the Spark; a file that declares two for the same DRAM is wrong, not refused
  (the evaluator cannot know), so the convention is one `unified_mem` resource.
- `resource kind=launch`: a serial queue. Launch cost is paid once per op, one op at a
  time per launch resource, before the op's own work. It is additive only when serialized;
  two engines with two launch resources overlap their launches.
- `op`: `t_ns` is the operation's own busy time on its engine (compute floor, `F/P`).
  `bytes`/`bw` is its traffic on a bandwidth resource (traffic floor, `Q/B`). The op's
  ideal floor is `launch + max(t_ns, bytes/cap)`; v1 assumes perfect overlap of its own
  compute and traffic inside one op (the issue's stated floor). `kind=barrier` has no
  cost and only orders. `kind=matvec` with `M`,`N`,`core`,`arm` and no `t_ns` gets its
  `t_ns`, `lo`, `hi` from `rx_costmodel` (`rx_cm_predict`) using the blob named by
  `calibration model=`; if that cell is not `known`, the op is `unknown` and the DAG is
  refused. `kind=storage` and `kind=persist` are ops whose `bw` is a storage resource.
- `dep a b`: b starts after a ends. Cycles are refused.
- `buffer`: live from the start of `first` to the end of `last`. Missing `first` means
  live from t=0 (resident state); missing `last` means live until the end. Peak live bytes
  per capacity resource must not exceed `cap`, else `REFUSED_CAPACITY` naming the time
  and the buffers.
- `sync id=X ops=a,b,...`: declares a barrier op `X` (no cost, no engine) that completes
  when every listed op has completed. Order later work after it with `dep X y`. A barrier
  with a measured cost is written as `op id=X kind=barrier t_ns=... prov=...` plus `dep` lines.
- `energy`: `instrument=none` makes every energy figure `UNMEASURED`. A named instrument
  with `joules` and a receipt is a measured whole-run figure for the named boundary; the
  evaluator never splits it per op. `power_w` with `prov=synthetic` allows a labelled
  sensitivity figure `E = P * T` that is never ranked as real energy. `TDP` is not an
  instrument.
- `removed`: minimum-work accounting (parent clarification). An op listed here is absent
  from the transformed DAG; the record keeps why. `reused`/`lookup` need `ref=`; a
  remaining op that depends on a removed op whose reason is not `reused`/`lookup` is a
  refusal (`REFUSED_REMOVED_REQUIRED`: required work cannot be removed by assertion).

## What a result must carry

Every evaluator output names: schema version, `machine.id`, `build.sha`,
`calibration.id` and `load1`, `asof`, the status (`OK`, `REFUSED_*`), the set of
provenances that fed the result, makespan nominal/lo/hi, the critical-path bound and each
resource-work bound (both must be <= makespan), peak live bytes, energy status, the
attribution ledger (compute, bandwidth, contention, launch, serialization, sync,
residual), and the counterfactual sensitivities. A result whose inputs include
`synthetic` is marked `synthetic=1`. A result never contains a number for an `unknown`
input.

## Pinned sources for v1 fixtures

- Spark host `spark-b87b`, GB10, 20 cores, 128 GiB unified memory (specified), DRAM
  273 GB/s (specified).
- M1 hardware passport, omega-mv#578, receipts `~/yardsticks/aien-model/M1/` (measured,
  load 15 to 29, omega f4a9849f): block1 realization call path 19.4 to 22.8 us per call;
  block2 GPU submit lifecycle 36.8 to 41.6 ms per job (fixed, size independent); block4
  CPU memcpy 1 thread 17.1 to 18.5 GB/s read+write at 1 GB, 8 threads 60.8 to 69.8 GB/s
  at 1 GB; GPU weight stream 218.6 GB/s from handoff 2026-10-19-qwen38-decode-speed.md
  (not re-measured in M1).
- Not available on 2026-10-19 and therefore `unknown` in every fixture: aien-architecture
  #244 traces, #252 clock calibration, #253 hardware atlas, #274 lifetime maps (all
  NOT_RUN at the time of freezing). Fixtures that need them refuse.
