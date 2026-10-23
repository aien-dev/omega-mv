# Preregistered qualification for omega #453

Registered before implementation and counter collection, 2026-10-14.
Baseline: 265ce82a1c220caa5f027abe068ca3aa813c633c. Agent: gb10-D.

Host: spark-b87b, aarch64. No sudo, sysctl changes, GPU workloads or model
inference. Acquire nonblocking /tmp/aien-gb10.lock for actual PMU runs;
if busy, record NOT_RUN. Record load, kernel, compiler, source and binary
SHA256, sibling source SHAs, sysfs types/cpus/MIDRs/event descriptions.

One event per trial, pinned to one allowed CPU from each verified core class.
Events: original six plus br_retired, br_mis_pred_retired, stall_frontend,
stall_backend. Verify every encoding from sysfs before attempting open.
Do not substitute speculative br_mis_pred for retired mispredictions.

Three repetitions each of fixed, predictable and difficult built-in loops;
1,000,000 iterations, fixed seed. Predictable versus difficult is a sanity
control, not a performance claim. Record generated assembly (conditional
branches must survive compilation). Expect positive cycles/instructions and
retired branches; inspect repeated difficult-versus-predictable misses only
when all inputs are OK, without an optimization approval threshold.
If permission is denied, stop hardware trials after one attempt per core
class; mark the trial matrix NOT_RUN with denial receipts. No fabricated
zeros or fabricated significance. Do not infer observed hardware support
from successful event discovery alone.

Host-independent negative controls: absent/malformed/mismatched event;
unknown MIDR; wrong PMU CPU membership; invalid CPU list; denied open;
zero running time; running > enabled; partial read; failed ioctl; failed
pin; multiplexed time. Invalid/unavailable records have null raw counts.
Deliberate mutations must show denial and multiplexing cannot become OK.
Legacy six-event names/codes and no-argument dispatch remain unchanged.

External attachment, if implemented, runs an argv directly (no shell),
counts only its initial task on the selected CPU, and explicitly states
that descendants/other CPUs are excluded. Since arbitrary code can change
its affinity, incomplete running time cannot be certified as multiplexing
for external workloads. No scaled counts or inferred branch-miss ratio.
