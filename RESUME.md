# Workflow fusion: resume note (2026-09-28, before the Spark power-off)

Done: OMEGA_WORKFLOW_FUSION merged to omega main (PR #61, merge ceb68d6).
Receipts in evidence/WORKFLOW_FUSION/ (latest candidate 05ca731, bound, PASS).
Golden-path session notified with the interface.

Next (optional follow-ups, none blocking):
- Compiled fusion's wall-clock latency is not better (within noise); find out why
  the single fused reaction isn't faster than 4 small ones on a quiet machine.
- Observation/proposal as resident reactions (currently host calls).
- Clean the /tmp/rx-fusion-* generation store dirs the test leaves behind.
