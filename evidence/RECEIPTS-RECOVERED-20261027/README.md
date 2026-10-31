# Receipts recovered on 2026-10-27

`docs/OMEGA.md` cited several runs whose logs were in no repository ("receipt unpushed, carried with the rebase") or whose only source was a path on the DGX Spark. Every file in this folder is a byte copy of a file found on the Spark on 2026-10-27, with no edits. Each subfolder has a `SHA256SUMS` computed with `sha256sum` on the copied bytes. The copies were made with `cp -p`, so the file mtimes are the originals (the one exception is the excerpt in `l354-door-one-row-20261021/`, see below).

Nothing here was re-run. These are the original logs of the original runs, with what they do and do not show.

## c1-omegese-gpu-front-chip-20261022 (bar C1)

- Original path: `~/handoffs/evidence-L386-chip-20261022/omega-omegese-gpu-front/`. That folder's `SOURCES.txt` (copied here as `SOURCES-evidence-L386-chip-20261022.txt`) says the logs came from worktree `omega-omegese-gpu-front`, branch `omegese/gpu-front`, head d1ac019ef604982d3dd3456b8d339ab3b78aa1b6, copied 2026-10-24T22:56:30Z. Branch `omegese/gpu-front` d1ac019e does not contain these logs.
- Run: 2026-10-22, 07:03Z to 07:05Z UTC (mtimes 02:03:32 to 02:05:51 -0500).

| File | Commit, binary | Verbatim final line | What it shows |
| --- | --- | --- | --- |
| `build-host.log` | 3aafd8a8 | `EXIT_CODE=0` | host suite `test_osc_gpu: 1308 checks, 0 failed` |
| `chip-red.log` | 3aafd8a8, binary b5c0674a... | `EXIT_CODE=143 (expected 1 with value mismatches)` | mutant run before the close fix; killed, no verdict |
| `chip-red-deadlock.txt` | | `ptrace: Inappropriate ioctl for device.` | the attempt to attach a debugger to the hung mutant run |
| `chip-red-deadlock-repro.log` | | `EXIT_CODE=124` | the hang reproduced under a 10 s timeout |
| `rebuild-close.log` | | `EXIT_CODE=0` | rebuild after the close fix |
| `chip-red-after-close-fix.log` | mutant 1 | `EXIT_CODE=1` | `643 checks, 14 failed`; includes `FAIL: gemm: chip launch -2: gemm launch: marker wait timed out (latched)` and `FAIL: attn: not run, an earlier chip failure` |
| `chip-green-after-close-fix.log` | d1ac019e, binary cb74c2ba... | `EXIT_CODE=1` | mutant 0: `645 checks, 5 failed`: ops_f op 8 (1 wrong), vadd 996 wrong words, vadd short inputs 896 wrong words, vadd short inputs status 0 (want 3), gate 939 wrong words. om_gemm and om_attn print no failure line in this run |

Correction to `docs/OMEGA.md` (bar C1, status 2026-10-24): the latched GEMM launch and "attention not run" come from the mutant run (`chip-red-after-close-fix.log`), where a failure is the expected result. The non-mutant run (`chip-green-after-close-fix.log`) failed on vector add, the gate kernel and one ops_f op. It printed no failure for om_gemm or om_attn. The log does not list which checks those two kernels ran, so this is not a GEMM or attention PASS. Bar C1 stays FAIL.

## c2-r2-hello-fresh-checkout-20261022 (bar C2)

- Original path: `~/handoffs/evidence-L386-chip-20261022/omega-runtime-r2-hello/fresh-hello.log`, mtime 2026-10-22 02:06:27 -0500. Source worktree `omega-runtime-r2-hello`, branch `runtime/r2-hello`, head 0f44b52bf43cbd21241ad269d5c21493c5863ffc.
- Run: 2026-10-22T07:06:05Z, a fresh clone in `/tmp`, commit 0f44b52b, `make hello-gpu`.
- Verbatim final line: `EXIT_CODE=0`. The result lines: `checked 1048576 of 1048576 elements: 0 wrong, 0 never written`, `PASS`, `# fresh-checkout-hello exit code: 0`.

Two corrections to `docs/OMEGA.md` (bar C2):
- The log records ONE run, not two. Lines 100 to 104 repeat the tail of the same output (line 100 starts mid-sentence); they are not a second run.
- The clone URL was `git@github.com:aien-spark/omega.git` (SSH, an authenticated account), not the README's `https://github.com/aien-dev/omega-mv`. So this is not yet "only the README commands from an empty folder".

## l354-door-one-row-20261021 (section 5, one-conversation figure "20.1 ms")

- Original path: `~/.local/state/aien-q36/batch-18089.log`, the log of a test server (port 18089) started by `serve-batch.sh` (aien-sovereign-core `serve/q36-batch-v2` 9c62690a, `evidence/q36-batch-20261021/serve-batch.sh`). Full file: 349,359 bytes, mtime 2026-10-21 10:06:22 -0500, sha256 9f2f1c9fbff24f6a45043947b3bfcac9686e708f0b3b931690e8e95e21cb09d1. It holds nine runs.
- `batch-18089-lines-1-114.log` is lines 1 to 114 of that file (`sed -n 1,114p`), the whole first run: `start batch door real8 (bin-16b27643, 8 slots, ctx 2048, swap=none)` at 2026-10-21T12:52:02Z. Its mtime is the time of the excerpt, not of the run.
- Verbatim final line: `aien-serve-http: shutdown complete in 0 ms`.
- The figure OMEGA.md cites is line 110: `qwen36-batch: spell over: 14 beats, 73 steps, 73 row tokens (10 decoded), 20.1 ms/step wall, chip 20.1 ms/step, spell 1.5 s`, and line 111, `prompt 63 tok, reply 10 tok, ... decode 201 ms (49.75 tok/s)`. That is one request with 10 decoded tokens, decode only (the 1,274 ms prefill is excluded), on a shared chip. It is not an end-to-end figure and not a quiet-window measurement.

## q36-ceiling-notes-20261020 (section 5, "Physical ceiling here" column)

- Original path: `~/handoffs/2026-10-20-q36-ceiling-numbers.md`, mtime 2026-10-20 17:06:37 -0500, 26 lines.
- These are MODELED numbers (bytes per token from the safetensors headers, divided into the measured 207 to 237 GB/s), not a run. The note says the arithmetic was checked in Wolfram by the coordinator on 2026-10-20; the calculation records themselves are not in this file.
- Verbatim final line: `What this means for the engine: past about 8 heads the DeltaNet state is the biggest mover of bytes. Levers it names: state in bf16 (measure the drift), and candidate heads on ONE sequence (MTP / tree verify) read and write the state once for k tokens, so they amortize the state as well as the weights; independent sequences do not.`

## Not found

- "2.95 percent of a word (efficiency review G1)", cited in `docs/OMEGA.md` section 5: no file on the Spark or in any repository contains it, except copies of `docs/OMEGA.md`. NOT_REPRODUCED; re-run needed.
