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
| C | one chain of receipts: every bundle names the previous bundle digest for its repo and the node signs over both; verify-chain refuses a rewritten history; two registered nodes agreeing on the exact head = merge-grade, with a spot re-check by ours; every seed rung carries the digest of the rung that built it | verify-chain in aien-test, bundle v2 (old bundles still verify), rewritten-history test fails; two receipts on one real head | L119, L120, L122 |
| B | the box: a broker owns the chip, the quiet state and one queue; waiting happens in the queue, never inside a lock; measurements one at a time in windows; builds cached by source hash; chosen by replayed wait times | replay numbers published; installed or not by the numbers | L113, L118 |

Rung 4 (the fuller Omega language: bytes, big storage, I/O) comes after R proves what the runtime actually needs;
Drake's rule: the compiler needs only the LLM plus calculations and semantics.

## Experiments that inform the line (not the line itself)
- No words: words vs meaningless symbols vs numbers under the same mask; prediction before run (L108).
- Founding check: which OSC-0 rules are enforced by a gate today and which are only written down (L114).
- Driver floor: keep / replace / already ours, per call (L101).

## The handover point (Drake, 2026-10-19)

The seed chain grows upward (rungs 0 to 4) while the C-built compiler grows to OSC-4 (floats, L135). They meet
at one point: the day the seed-born compiler can compile the OSC-4 compiler's own source. The gate at that point:

1. The C-built OSC-4 compiler and the seed-born compiler compile the same fixed corpus (every .osc and .omegese
   test, the decode kernels' listings, Atlas) to byte-identical IR and machine words.
2. The seed-born compiler compiles its own source; that result compiles the source again; the two are byte-identical.
3. Both builds of the corpus are checked by two independent compilers (the archived C one and the seed-born one)
   and agree bit for bit (the trusting-trust check).

Then the handover: everything above OSC-4 is rebuilt from the seed-born compiler; the C-built compiler leaves
the build and is archived, pinned, as the oracle. Nothing C-built remains in the running system except the floor
(rung F). Rung 4 is defined by this: "the fuller Omega" is exactly the subset needed to compile OSC-4's source,
nothing more.

## Decisions taken from the archives (B to C, read at the source; ~/handoffs/2026-10-19-b-to-c-archives.md)

Every bandage C kept was a fix for a 1969-1973 constraint (8K to 24K of memory, a one-pass compiler, keeping old B
code working) that Omega does not have. Decided 2026-10-19 by the historical and physical reason, each with its gate:

| # | decision | why (their words, their cost) | gate |
|---|---|---|---|
| 16 | every buffer carries an explicit length; no terminator byte | the null terminator dodged an 8-bit count slot; Ritchie: code "must occasionally search for the end of a string" | no scan-to-end in checker2 or codegen; test refuses a length-less buffer |
| 17 | arrays are values with shape; they never become pointers | decay was kept so old B code would run; Ritchie: "hard to compile into efficient code on vector machines" | IR has no array-to-pointer conversion; twin gate unchanged |
| 18 | two live references may not overlap unless proven | aliasing is why "optimizers must be cautious"; C bolted on `restrict` later | checker2 proves disjointness or refuses; receipt records it |
| 19 | no undefined behaviour anywhere in the spec | the C standard "did not attempt to specify formally the language semantics"; a receipt cannot sign a meaningless program | every spec sentence has a test or a trap; a grep for "undefined" in spec/ finds none |
| 20 | width and signedness explicit in every type; no implicit conversions; no `char` | "does not survive transportation between machines"; width is the cost in a bandwidth-bound system | lowering refuses any implicit widen/narrow; byte is a width (u8), never a character |
| 21 | no preprocessor, no textual inclusion; one program, one check | an "optional adjunct" with "incomplete integration"; missing argument types came from avoiding cross-file checking | the compiler reads one unit; imports resolve by digest (OSC-VC1), never by text |
| 22 | grammar regular and left to right, one legal next word | inside-out declarations and `&` vs `==` are admitted mistakes; the masked writer needs unambiguous next words | `omegese next` returns one rule per prefix; no prefix has two parses |
| 23 | bounds proved at check time, not run time | shapes for one workload on one machine are known before the kernel exists | no bounds check on the per-token GPU path; checker proof in the receipt |
| 24 | allocation fixed at check time: no manual heap, no garbage collector | "C is hostile to automatic garbage collection"; manual freeing puts "the burden on the programmer" | pools sized at check time (decision 7); no allocation on the per-token path |
| 25 | competitors are baselines, never the finish line; the target is the physical limit | Drake 2026-10-19, walking back "no competitor comparison": "I don't want us to compare to competitors or stop when we beat them. I want us to keep going and compare to the physical limit of reality." | every speed receipt leads with fraction of the physical ceiling (bytes moved per token over memory bandwidth; joules per token over the energy floor); outside engines (llama.cpp, vLLM, TensorRT-LLM) are measured on the same GB10, same model, same text, as a reference row only; beating them closes no item |

Kept from their story: bootstrap early and watch the size delta; the acceptance test is a number against the best
hand-written kernel; every feature justified by the one real environment (one model, one machine).

## The chain, explicitly

What blockchain is good for, kept; what it wastes, left out. Kept: a hash chain (each record carries the fingerprint of the one before, so a rewritten past breaks every later link), multi-party agreement (no single machine is final), and standing earned by useful work (judging PRs). Left out: coins, tokens, proof-of-work puzzles, global consensus protocols. The registry of nodes is a file Drake merges. The receipts shelf git history is the public ledger; the seed chain is the same shape for the compiler itself.

## How the pieces join
- One repo per layer, one branch per rung, one desk item merges in rung order (L095 for structs; a desk item per rung as each READY lands).
- Every rung's artifact names its parent rung in its README; an artifact built by an outside tool says "on loan" and names its gate.
- Every number: receipt path, window id, box load. A number without those is not evidence.
- Future, on the ledger and not started: decodable bitstream for T (L111); Omegese import (L096).

## What Drake does
Reads this page and the READY lines. Presses the button on the iMac bundle. Decides when a PROPOSED decision becomes a decision. Nothing else is asked of him twice.
