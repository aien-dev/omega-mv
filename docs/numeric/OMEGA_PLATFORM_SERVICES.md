# Omega platform services and shared push-buffer encoding (L202, aienos#286 cut A)

Companion of `docs/numeric/OMEGA_GPU_NATIVE_BOUNDARY.md` (the GPU platform contract and the
freestanding closure) and of the dependency map in aienos
`docs/GB10_NATIVE_DEPENDENCY_MAP.md` section 3. Status words: DONE (host evidence on this
branch), NOT_RUN (needs the GPU or the native system), PLANNED.

## 1. What this cut adds

| Piece | File | What it is |
|---|---|---|
| Platform services contract | `src/omega_platform_services.h` | One table of host services (ABI 1.0): CLOCK, WAIT, YIELD, LOCK, TASK, NAME, PIN, HEAP, CONFIG, DIAG. Typed lock and task handles (index + generation). |
| Contract check, stub, install | `src/omega_platform_services.c` | `omega_psvc_check`, the refusing stub, `omega_psvc_install`, the convenience calls, `omega_psvc_diagf`. Freestanding; in the closure. |
| Hosted table | `src/omega_platform_services_hosted.c` | Each entry is the exact libc call it replaces (table below). Hosted only, never in the closure. |
| Shared encoding | `src/omega_gpu_encoding.h` | `omega_gpu_mthd` (physics `nvrm_mthd`) and `omega_gpu_gp_entry` (physics `nvrm_gp_entry`), pure inline. |
| Formatter with `va_list` | `src/omega_gpu_fmt.c` | `omega_gpu_vfmt`; `omega_gpu_fmt` now calls it. |

### Hosted table: the libc call behind each service

| Service | Required | Hosted call | Callers moved in this cut |
|---|---|---|---|
| CLOCK | yes | `clock_gettime(CLOCK_MONOTONIC)` in ns | (none yet; the session and APIs use `OmegaGpuPlatform`'s clock) |
| WAIT | yes | `usleep(us)` | seat: the marker wait (50 us), heartbeat wait, lease tick, lease sample, kill wait |
| YIELD | no | `sched_yield()` | none yet |
| LOCK | yes | `pthread_mutex_init/lock/unlock/destroy`, 64 slots | none yet (the world lock stays in `rx_world`; the seat reads the seat generation through `rx_world_seat_generation`) |
| TASK | yes | `pthread_create(NULL attrs)`, `pthread_join(NULL)`, 64 slots | seat: launch thread, lease thread |
| NAME | no | `pthread_setname_np(pthread_self(), name)` | seat: "rx-seat-lease" |
| PIN | no | `pthread_setaffinity_np` over `CPU_ZERO` + `CPU_SET((int)cpu)` | seat: `RX_SEAT_LEASE_CPU` |
| HEAP | yes | `malloc`, `calloc`, `free` | seat: code, seat, job, returned image |
| CONFIG | no | `getenv` (unset gives `NOT_SET`, empty gives `OK` with "") | seat: `RX_SEAT_LEASE_CPU` |
| DIAG | no | `fputs(text, stderr)` | seat: every former `fprintf(stderr, ...)` line, formatted by `omega_gpu_fmt` |

Refusal policy (in the header): a refused required service fails the operation that asked;
a refused optional service means "go on without it". The seat checks the required set before
it touches the device (`rx_gpu_seat_begin`), so under the freestanding stub it refuses at once.

### Why two tables

`OmegaGpuPlatform` is the per-device platform the GPU session negotiates (device, DMA,
queues, completion, and the host clock, wait, lock and memory the session needs while it holds
the device). `OmegaPlatformServices` is for code that runs without a session (the resident seat,
later the one-shot launchers) and for services a session never needs (tasks, names, pins,
configuration). The hosted `OmegaGpuPlatform` (`src/omega_gpu_platform_linux.c`) keeps its own
libc calls on purpose: it is on the token path and this cut leaves its object code unchanged.

## 2. Callers moved

- `src/runtime/rx_resident_gpu.c` (the resident seat): no `pthread_*`, `usleep`, `getenv`,
  `atoi`, `malloc`/`calloc`/`free`, `fprintf` or `snprintf` left. Its buffers are `SeatBuf`
  (an `OmegaCpuView` and an `OmegaGpuVa` as distinct values, the driver's `NvrmMem` inside, seen
  only by the driver calls). Its method headers use `omega_gpu_mthd`. Its device calls
  (`m16_native_*`, `nvrm_*`) remain: the seat moves onto `OmegaGpuPlatform` in cut E.
- `src/omega_gpu_elementwise_api.c`: heap, clock and text through the session
  (`omega_gpu_host_*`, `omega_gpu_session_now_ns`, `omega_gpu_fmt`), as the attention API
  already did. It joins the freestanding closure.
- `src/omega_gpu_session.c`, `src/omega_gpu_cmdlist.c`: their private `bw_mthd` copies are gone;
  they use `omega_gpu_mthd`.

## 3. Not in this cut (stated, not missed)

- File access (`fopen`, `realpath`): `src/omega_blackwell_submit.c` (vector executor, Linux
  private, sha256-pinned by the M17/M18 gates) and `src/omega_physics_dir.h` (host tooling).
- The numeric launchers (`src/omega_numeric_*_gb10.c`, `src/omega_numeric_native.h`) keep
  `NvrmMem` and their CPU-path `malloc`. No host harness drives their device path (the
  `test-*-host` targets build them CPU-only), so a rewrite here could not be measured; they move
  in cut E behind a fake-driver test like the seat's.
- `src/omega_blackwell_engine.c`, `src/omega_blackwell_submit.c`, `src/omega_accelerator_world.c`,
  `src/omega_world_gates.c`: the Linux reference and host tooling (boundary doc section 3).
- `omega_gpu_gp_entry` has no Omega caller: the only GPFIFO writer is physics `nvrm_enqueue`. The
  header is the provider for a native queue; the sweep test is its only consumer.
- PHYSICS itself is unchanged (another repository and a pin bump); Omega no longer re-copies the
  encoder and proves its copy equal.

## 4. Evidence (host only)

Gate runner `~/handoffs/L202/gates.sh` (pinned physics `physics-pf-6d7cf0d4`, no device).
Results are in `evidence/OMEGA-CUT-A-SERVICES/` with command and exit code per gate, before and
after.

- `make test-gpu-encoding`: both encoders equal the physics originals over 2^30 legal headers
  plus 2^24 random arguments, and 2^22 + 1 word counts at 16 edge addresses plus 2^24 random
  entries; a mutant control (one wrong header, one wrong entry) must fail and name both.
- `make test-platform-services`: the contract test on the hosted table and on the freestanding
  objects (stub default, install, every check refusal, typed handle staleness and limits, config
  unset versus empty, task name and pin, diag text equal to `snprintf` for every seat format).
- `make test-rx-seat-fake`: the seat over the simulated driver (`tests/fake_m16_native.c`): open
  refused, then begin, heartbeat, lease, kill, relaunch, finish, unpinned and pinned. Its stdout
  (driver events, allocation sizes, submitted packet headers, release payloads, seat results) and
  stderr (the seat's own lines) are byte-identical between the old and the new seat.
- Object identity: the disassembly of all 82 objects under `build/` compared before and
  after. 80 are identical, among them the session, command list, matmul and attention APIs,
  decode graph and the Linux GPU platform. The two that differ are the two this cut changes
  on purpose: the elementwise API and the formatter.
- The 17 existing host gates keep their exit codes (16 at 0; `test-gpu-attention` at 2 with
  the same 16 pre-existing failures as main).

NOT_RUN: any GPU run (the GB10 golden-token check, the R12-R16 silicon gates). The brief for this
cut forbids device access; the substitute evidence is the fake-driver trace and object identity.
No speed claim is made.
