# RED receipt (unfixed encoder), kept on purpose

This run is the chip FAIL of the prefill attention (omega-mv#545) on the code before the encoder fix:
`omega_blackwell_codegen.c` wrote a fixed word 3 for IADD3 (`0x010fca00`: stall 5, wait on scoreboard 4) and dropped
the emitter's control word. At attend IR index 55 the emitter scheduled a wait on the Q loads' scoreboards (0x3);
the chip got a wait on scoreboard 4, the MOVs at 88/89 zeroed the loads' address registers R22:R23 while loads were
still queued, and the attend faulted (Xid 13, Graphics SM Warp Exception: Invalid Address Space) once a second wave
of CTAs ran behind the first wave's stores (grid > 48 CTAs).

INVALID lines in this run: every "mutant ... caught" and every "process latched" result after the first marker
timeout. The process was latched by the first fault, so those checks never ran a kernel.

Investigation: ~/workspace/investigations/2026-10-18-pattn256-hang (run10..run21). The proof run (run21): same
binary, only the 7 IADD3 words rewritten to the emitter's control word: 5/5 at 104 tokens, 2/2 at 2048 tokens,
1/1 at 2048 tokens from 6144; the unchanged binary in the same batch FAILED.
