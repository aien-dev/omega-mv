# GPU channels: more than one command queue (L136, roadblock R3, OSC-0 decision 12)

Written before the measurement. The receipt goes in `evidence/q38-channels-*`.

## What a channel is

A channel is one command queue into the GPU: a ring of work entries plus a doorbell. Until now the engine opened one
(`src/omega_gpu_platform_linux.c`, `max_queues = 1`), so every job from every caller waited in one line.

Cost of a second channel (physics `doc/driver-floor` DRIVER_FLOOR.md): one more BLACKWELL_CHANNEL_GPFIFO_A and
BLACKWELL_COMPUTE_B in the same KEPLER_CHANNEL_GROUP_A and FERMI_CONTEXT_SHARE_A, one UVM_REGISTER_CHANNEL, one
GET_WORK_SUBMIT_TOKEN, all before the single GPFIFO_SCHEDULE. That is setup, paid once at device open. A submission
after that is two stores (ring entry, doorbell), the same as channel 0. Physics `feat/gpu-channels` 7476de7 adds this as
`nvrm_channel_n`; the platform opens it only when `AIEN_OMEGA_GPU_CHANNELS` is 2..4. Unset, the code path is the
single channel exactly.

## The arithmetic for one decode token of the 27B (Qwen3.8-27B, FP8 weights, bf16 head)

- Weights read per token: 26.9 GB. At the memory ceiling of 273 GB/s that is 10.15 tok/s (calc:41e5e25b).
- The token is a chain: each layer needs the previous layer's output, so layer N+1's weights cannot be "loaded early".
  On this machine the weights are not copied anywhere before use; the multiply kernel reads them straight from memory.
  Reading them IS the work. The L2 cache (tens of MB) is far smaller than one layer (~380 MB), so there is nothing to
  prefetch into.
- Timeline receipt `evidence/q38-decode-speed-chip-20261019T0316Z` (4 layers + head): the weight multiplies take
  18584 of 18866 us; everything else (norms, attention, GDN step, gaps between launches) is 1.49% (calc:b581cb99).
- The one independent pair inside a token (the small a|b multiply beside the big mix multiply in GDN layers) already
  runs side by side on ONE channel (`omega_gpu_cmdlist_mark_independent`, `AIEN_OMEGA_Q38_OVERLAP_AB`, default on).
  The brief's fallback (overlap by reordering on one channel) therefore already exists and is installed.

So splitting one token across two channels can win at most ~1.5% for one request, minus the cost of the fences that
order the two queues. Not worth building first.

## Where a second channel could help: two requests at once

Each weight multiply reaches 193 to 229 GB/s of the 273 GB/s ceiling (same receipt). If a single multiply is limited by
how many memory reads one grid keeps in flight, two grids from two requests running at the same time could pull more
together. Ceiling of that gain: 273 / 220 = 1.24x aggregate tokens per second (calc:91c5deff). Requests do not share
weight reads this way; each still reads all 26.9 GB.

Unknowns, said out loud:
1. Whether the GPU runs work from two channels of one channel group at the same time, or takes turns (time slicing).
   The CUDA guide says separate queues remove false dependencies but "concurrent execution of independent GPU work is
   not guaranteed". DOCS SILENT on GB10 specifics.
2. Whether a WAIT_FOR_IDLE on channel 0 (every dependent launch has one) also waits for channel 1's grids. If it does,
   the two streams serialise at every barrier.

## Prediction (before the measurement)

Two independent 27B decode streams, 64 layers, 64 steps:
- serial (both on channel 0, one after the other): about 2 x 123 ms per step (one stream decodes at 8.13 tok/s on
  branch qwen38/decode-speed), so ~8.1 tok/s aggregate, the same as one stream.
- paired (one per channel, both in flight): between 1.00x and 1.10x of serial throughput. Most likely ~1.0x: each
  multiply already pulls ~80% of the bandwidth, and unknown 2 may serialise them anyway.
- Outputs bit-identical between serial and paired (the streams share only read-only weights).

## The bigger lever, for later

Batching: two requests multiply against the SAME weight read (a matrix times two vectors instead of one). That halves
the bytes per token for two requests, ceiling 2x aggregate, and needs a kernel change (GEMV to a thin GEMM), not a
channel change. It is the next step for throughput regardless of what the channel receipt says.

## Result (2026-10-20 03:09Z, evidence/q38-channels-g3-64x64, receipt ~/yardsticks/receipts/2026-10-19-l136-q38-channels-64x64.json)

Run: 64 layers at the real shapes (48 GDN, 16 attention, bf16 head; random weights, 26.9 GB read per token), two
streams, 64 interleaved steps per mode, then 64 serial-only and 64 pair-only steps as energy windows. Box load 54 to 64
(other lanes' builds and two 27B servers resident), so absolute speed is below a quiet box.

| | serial (one channel, A then B) | pair (two channels, both in flight) |
|---|---|---|
| median step (2 tokens) | 276.891 ms | 276.623 ms |
| aggregate | 7.223 tok/s | 7.230 tok/s |
| energy window | 17.786 s, 128 tokens | 17.983 s, 128 tokens |
| GPU J/token (sensor GPU W x time) | 5.77 (calc:21e7fd79) | 5.63 (calc:f386ed96) |
| wall J/token (UPS, whole rack) | 39.95 (calc:83e2aa95) | 42.15 (calc:37c409a8) |

- pair / serial step time = 0.9990. The prediction (1.00 to 1.10, most likely ~1.0) held: **two channels do not
  overlap the 27B decode on GB10.** Either the two channels of one group take turns, or each multiply already holds the
  memory system; this run cannot tell which (unknowns 1 and 2 above stay open).
- G2: both streams' 128 tokens and final x / logits identical bit for bit between serial and pair; negative control
  (A and B differ) passes. G1 (one channel identical to the base build) passed in evidence/q38-channels-chip-rerun.
- Decision 12 metric: tokens/s does not regress (0.999 step ratio); GPU J/token equal within noise. The wall figure
  differs by one UPS step (280 W to 300 W at 1792465865, inside the serial window, 10 W resolution, whole rack
  including other lanes), so it is not attributed to the channel.
- Distance to the ceiling: 7.230 tok/s aggregate = 0.712 of 10.149 (calc:3355b30d) on this loaded box.
- One driver out-of-memory message (22:09:55 CDT) fell in the window during weight upload; every allocation of this
  test succeeded (each is checked), so its source process is not identified.

Installed: the second channel stays opt-in (`AIEN_OMEGA_GPU_CHANNELS=2`, default one channel, today's path byte for
byte). It costs nothing when off and gives the server a way to keep two requests in flight, but it is not a speed lever
for this decode. The one-channel overlap (mark_independent) stays as it is. The next throughput lever is batching:
one weight read for two requests.
