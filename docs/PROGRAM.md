# The Omega Program: one line from the seed to the LLM

Status: the standing plan, 2026-10-19. Every item below is a line on the Spark ledger (`ledger list`). This page
is the only map; when the ledger and this page disagree, fix this page the same day.

Founding: docs/adr/OMEGA-SYSTEMS-CORE-0000.md stands as written. It is not re-litigated here. "Linear Fence"
(decision 3) means a completion ticket used exactly once; it never meant one job at a time.

## The one sentence
Omega exists to run this LLM on our own engine. Everything is born from one seed, measured in fraction of the
hardware ceiling and bits of TURING Yield per joule, and every outside tool is a reference check that leaves.

## The line (bottom to top)

| rung | what | gate (pass = byte-identical or a number with a receipt) | ledger |
|---|---|---|---|
| 0 | seed-0: a few hundred hand-written AArch64 words that turn hex text into bytes | rebuilds itself byte-identical; every word hand-decoded, 0 discrepancies | L103 |
| 1 | labels | rebuilds itself through seed-0 | L103 |
| 2 | one assembler, text tables per target: AArch64 (CPU) and sm_121 (GPU) | every word equals the C encoder / C codegen word; the manual decides disagreements | L103, L106 |
| 3 | first Omegese words to machine words, built by rung 2 | same results as the C path on the 10 dev tasks; word counts side by side | L103 |
| 5 | Atlas (the boot seed) reborn through rung 2, no GNU as/ld | byte-identical to the audited 1,480 bytes; QEMU gates pass | L105 |
| 6 | GPU words from the same chain, run on the chip through our libcuda-free launcher | bit-identical to today's codegen; one kernel runs | L106, L098 |
| L | Omegese: the AI's assembly language | structs (L093, L094, L095); the mask in the engine so a malformed program cannot be written (L112); contracts in the same mask so a wrong program is refused (next, after L094) | L093-L095, L112 |
| R | the LLM runtime, written in Omega: decode loop, sampling, mask, tokenizer, server door | each stage replaced with a byte-identical or receipt-equal gate against today's build; the lineage inventory sets the order (L107) and the chain map names the host-side C per token (L097) | L097, L107, L085, L089, L090 |
| M | the measure | fraction of the physical ceiling (QWEN38-SPEED gate, L068, now with J/token); bits of T per wall joule (TY-7, L109); one measurement window at a time with calibration and baseline (L113) | L068, L109, L113 |
| F | the floor we stand on and do not replace | the open kernel module's 8 calls, the signed GPU firmware, CPU microcode: named and sized (L101) | L101 |

Rung 4 (the fuller Omega language: bytes, big storage, I/O) comes after R proves what the runtime actually needs;
Drake's rule: the compiler needs only the LLM plus calculations and semantics.

## Experiments that inform the line (not the line itself)
- No words: words vs meaningless symbols vs numbers under the same mask; prediction before run (L108).
- Founding check: which OSC-0 rules are enforced by a gate today and which are only written down (L114).
- Driver floor: keep / replace / already ours, per call (L101).

## How the pieces join
- One repo per layer, one branch per rung, one desk item merges in rung order (L095 for structs; a desk item per rung as each READY lands).
- Every rung's artifact names its parent rung in its README; an artifact built by an outside tool says "on loan" and names its gate.
- Every number: receipt path, window id, box load. A number without those is not evidence.
- Future, on the ledger and not started: decodable bitstream for T (L111); Omegese import (L096).

## What Drake does
Reads this page and the READY lines. Presses the button on the iMac bundle. Decides when a PROPOSED decision becomes a decision. Nothing else is asked of him twice.
