# Inference-Cost Benchmark: Tokens per Verified Program

This benchmark measures the metric from Drake Stapleton's Omega/encoder essay,
**verified programs produced per unit of inference cost**. It compares Omega, C, Python and Rust on the
same 20 held-out tasks.

A model writes one function per task. The harness compiles it, runs it on hidden inputs and compares
the output. Token counts come only from the `usage` object the endpoint reports; nothing is estimated.

Two arms run for every language:
- **plain**: the same two messages (system, user) are sent for up to K attempts.
- **feedback**: after a failed attempt, the conversation grows by the model's code (assistant) and a
  user message holding the compiler's diagnostic, or "wrong results (exit status N)". Omega candidates
  are compiled as `om2 diag` units, so the feedback carries both `omc: line N: error K` and the
  `omc-diag 1 code=... line=... col=...` record.

Each task stops at its first verified attempt.

## Run against a real model

The model server must be up first. This harness never starts one.

```sh
sh bench/inference_cost/driver.sh --run-model --endpoint http://127.0.0.1:18091 --model <model-id> \
    --langs om,c,py,rs --arms plain,feedback --attempts 3
```

- `--endpoint` is required and has no default; requests go to `<endpoint>/v1/chat/completions` with
  `"stream":false`.
- The other options and their defaults are `--temperature 0.2`, `--max-tokens 4096` and
  `--out build/inference_cost/run-<utc>-<pid>`.
- A missing endpoint, a curl failure, a non-200 status, or a body without `usage` (or with an `error`
  member) prints `NOT_RUN: ...` and exits 3. Logs written up to that point are kept.

The run directory holds:

| file | content |
|---|---|
| `run.txt` | endpoint, requested model, `models_reported` (the ids the endpoint returned), options, omc sha256, times |
| `calls.txt` | one line per HTTP call: task lang arm attempt http_code model prompt completion total http_ms |
| `attempts.txt` | the attempt wire, version 2, read by `scorer.om` |
| `summary.txt` | the scorer report |
| `<lang>/<arm>/<task>/conv/` | the messages sent |
| `<lang>/<arm>/<task>/a<N>/` | request.json, response.json, candidate, compiler and run output |

The driver prints one line per (lang, arm): verified/attempted tasks, attempts, first-attempt
verified, total tokens, tokens per verified program, verified per 1k tokens and verified per minute.

## Not run yet

- **Every real-model number is NOT_RUN.** No real endpoint has been measured with this harness: the
  self-test talks only to `fake_model.om`.
- Wall time in the self-test is the fake model's latency plus local compile and run time. It says
  nothing about inference speed.

## Self-test and task check

```sh
sh bench/inference_cost/driver.sh --check-tasks   # every reference verifies, every mutant is rejected
sh bench/inference_cost/driver.sh --self-test     # also run by `make test-inference-cost`
```

`--self-test` performs these checks, in order:

1. **Build.** Checks the compiler pin: the sha256 of `seed/out/omc` must equal `seed/out/omc.sha256`,
   otherwise NOT_RUN exit 3. Then builds `scorer`, `chat` and `fake_model` into
   `build/inference_cost/bin`.
2. **Scorer rejections.** A wrong version gives 3, a truncated wire 2, and an unknown language 2.
3. **Chat edge.** An error object, a body without usage, and a non-JSON body each give exit 4.
4. **`--check-tasks`.**
5. **Fake-model runs.** `fake_model` listens on an ephemeral 127.0.0.1 port, and every reply reports
   usage 111/37/148. Four runs, all over om, c, py and rs:

   | mode | arms, attempts | expected |
   |---|---|---|
   | `good` | plain, 1 | 20/20 per language, with exact token sums |
   | `bad` | plain, 1 | every mutant compiles and none verifies |
   | `bad-then-good` | plain + feedback, 2 | feedback verifies all 20 on attempt 2 with both attempts' tokens summed; plain verifies 0 |
   | `refuse` | plain, 1 | compiles 0 and verifies 0, and no binary is ever run |

6. **NOT_RUN paths.** An unreachable endpoint (the fake is killed first), an HTTP 404, and a missing
   `--endpoint` each give exit 3.

## Files

| file | role |
|---|---|
| `driver.sh` | POSIX sh glue: assembles candidates, runs compilers and curl, writes records |
| `chat.om` | the only JSON code (composed with `seed/json/json.om` and `jsonw.om`). `chat req` turns the request wire into the OpenAI request body. `chat resp` reads a response: usage, model id, and the code block (after the last `</think>`, the first fenced block) on fd 3 |
| `fake_model.om` | an OpenAI-compatible fake on `seed/net/http.om`. Modes `good`, `bad`, `bad-then-good` (reference only when the conversation has 3 or more messages) and `refuse`. Uses headers `X-Bench-Task` and `X-Bench-Lang` |
| `scorer.om` | reads the attempt wire (v1 or v2) and writes the summary |
| `io.om` | stdin/stdout helpers appended after an Omega candidate |
| `prompts/system-<lang>.txt` | system prompts; the Omega one is a compact language primer |
| `tasks/taskNN/` | `statement.txt`, `spec.txt`, `inputs.txt`, `expected.txt`, and `reference.<lang>` plus `mutant.<lang>` for om, c, py and rs |

## Candidate assembly and verification

The model writes only the function. The harness appends a stdin driver generated from `spec.txt`: it
reads `inputs.txt` and prints one decimal per case. A candidate is verified when:
- it compiles with exit 0;
- the run exits 0 within 10 s;
- its output equals the cases of `expected.txt` byte for byte.

A compiler refusal is never verified and its output is never run.

| lang | assembled as | compile |
|---|---|---|
| om | `om2 diag`, candidate, `io.om`, generated `fn main` | pinned `seed/out/omc` |
| c | candidate, generated `main` | `cc -O2 -include stdint.h -include stdio.h -include stdlib.h ... -lm` |
| py | candidate, generated `_bench_main()` (the result must be an `int`) | `python3 -m py_compile`, run with `python3 -I` |
| rs | candidate, generated `fn main` | `rustc -O --edition 2021 -A warnings` |

For an Omega candidate, line 1 of the compiled unit is the added `om2 diag` line. A leading `om2...` line
written by the model is dropped.

## Wire formats (plain text, no JSON)

### Task specification (`spec.txt`)
```text
version 2
name <function_name>
param <name> <u32|i32|u64>      (one line per parameter, in order)
ret <u32|i32|u64>
```

### Hidden inputs and expected outputs
```text
version 1
cases <N>
<arguments for case 1, or its expected value>
...
```

### Chat request wire (`chat req`)
```text
version 1
model <id>
max_tokens <N>
temperature <JSON number>
messages <K>
msg <system|user|assistant> <NB>
<NB bytes>
...
```
A malformed wire exits 2; a writer refusal exits 3.

`chat resp` writes `version 1`, `model`, `prompt_tokens`, `completion_tokens`, `total_tokens` and
`code_bytes` lines. It exits 4 on a missing usage object, an error object, no choices, or a body that
is not JSON.

### Attempt records (`attempts.txt`)
Version 2:
```text
version 2
attempts <N>
<task_id> <lang> <arm> <attempt_num> <first_attempt> <compiled_rc> <verified_flag> <wall_ms> <prompt_tokens> <completion_tokens> <total_tokens>
```
- `lang` is one of `om c py rs`; `arm` is one of `plain feedback`.
- `wall_ms` runs from the request until verification ends (model latency plus compile and run).

Version 1 (still accepted) has no `lang` or `arm` column:
`<task_id> <attempt_num> <first_attempt> <compiled_rc> <verified_flag> <wall_ms> <prompt_tokens> <completion_tokens> <total_tokens>`.

Rejection rules:
- An unknown version exits 3.
- A line count different from `attempts <N>`, a non-numeric field, or an unknown lang or arm exits 2.
- Input of 983040 bytes or more exits 2.

### Summary (`summary.txt`)
A version-1 input gives the version-1 summary (`version 1`, `tasks_total` ... `status`).

A version-2 input gives one block per (lang, arm) present, in the order om, c, py, rs × plain,
feedback:
```text
version 2
groups <G>
group <lang> <arm>
tasks_total <N>
attempts_total <N>
compiled_count <N>
verified_count <N>
first_attempt_verified <N>
total_prompt_tokens <N>
total_completion_tokens <N>
total_tokens <N>
total_wall_ms <N>
total_wall_seconds <N>
tokens_per_verified <fixed3>|none
verified_per_1k_tokens <fixed3>
verified_per_minute <fixed3>
status PASS|FAIL
end
...
status PASS|FAIL
```
- A group's `status` is PASS when every task in it verified.
- Exit status: 0 when every group passes, 1 when any fails (a valid result for a model run), 2 for
  malformed input, 3 for an unknown version.

The rates:
- `tokens_per_verified` $= T_{\text{total\_tokens}} / V$
- `verified_per_1k_tokens` $= (V \times 1000) / T_{\text{total\_tokens}}$
- `verified_per_minute` $= (V \times 60) / T_{\text{total\_seconds}}$

## Tasks

| task | function | computes |
|---|---|---|
| `task01` | `clamp_add_u16` | saturated 16-bit addition |
| `task02` | `absdiff_i32` | exact $\lvert a - b\rvert$ as u64 |
| `task03` | `popcount_u32` | set bits |
| `task04` | `gcd_u32` | greatest common divisor |
| `task05` | `isqrt_u32` | $\lfloor\sqrt{x}\rfloor$ |
| `task06` | `collatz_steps_u32` | steps to 1 under $3n + 1$ |
| `task07` | `bitrev_u16` | 16-bit reversal |
| `task08` | `fibonacci_u32` | $F_n \bmod 2^{32}$ |
| `task09` | `factorial_u32` | $n! \bmod 2^{32}$ |
| `task10` | `is_prime_u32` | primality |
| `task11` | `parity_u32` | bit parity |
| `task12` | `clz_u32` | leading zero bits |
| `task13` | `ctz_u32` | trailing zero bits |
| `task14` | `power_mod_u32` | $a^b \bmod 2^{32}$ |
| `task15` | `min3_i32` | minimum of three |
| `task16` | `max3_i32` | maximum of three |
| `task17` | `sum_digits_u32` | sum of decimal digits |
| `task18` | `reverse_digits_u32` | reversed decimal digits |
| `task19` | `hamming_dist_u32` | Hamming distance |
| `task20` | `rot_left_u32` | 32-bit left rotation |

Each `statement.txt` is language-neutral; the per-language signature is added to the user prompt.
