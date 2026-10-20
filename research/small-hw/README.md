# Small hardware research (ledger L273)

What is here: the measured probe and the arithmetic behind `docs/speed/SMALL_HARDWARE.md`.

- `moe_stream/`: runs the Qwen3-30B-A3B on the CPU with its experts read from disk at a capped speed (O_DIRECT, paced
  reader, LRU expert slots) and checks every word and every logits vector bit for bit against the model held fully in
  memory. Built from a read-only export of aien-sovereign-core main (`build.sh`); runs on the DGX Spark. It is a
  stand-in for a small box's disk, not a run on a Pi.
- `calc-receipts.txt`: every derived number with its aien-calc receipt id.

Shown on real hardware: the Spark only (CPU and NVMe). Nothing here has run on a Pi, a Mac or the PC yet.
