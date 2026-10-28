# Inference-Cost Benchmark (Held-Out Evaluation)

This benchmark implements the metric articulated in Drake Stapleton's Omega/encoder essay:
**Optimize for verified programs produced per unit of inference cost.**

Rather than constraining vocabulary words to a single model's tokenizer (such as Qwen), the language
interface evaluates empirical model efficiency across held-out coding tasks. Progress is measured by:
- First-attempt correctness
- Successful compilation (`omc` return code 0)
- Verified behavior (all hidden test cases pass)
- Completion time (wall seconds / milliseconds)
- Total compute cost (prompt tokens + completion tokens)

Reported summary metrics:
- **`verified_per_1k_tokens`**: $(V \times 1000) / T_{\text{total\_tokens}}$
- **`verified_per_minute`**: $(V \times 60) / T_{\text{total\_seconds}}$

## 1. Line-Oriented Wire Format (No JSON)

All data files use versioned, line-oriented plain text with explicit rejection rules.

### Task Specification (`spec.txt`)
```text
version 1
name <function_name>
params <param_types...>
ret <return_type>
```
Rejection:
- First line must be `version 1`. Any other version is rejected with exit code 3.
- Malformed header or missing fields reject immediately.

### Hidden Inputs (`inputs.txt`)
```text
version 1
cases <N>
<arguments for case 1>
...
```

### Hidden Expected Outputs (`expected.txt`)
```text
version 1
cases <N>
<expected return value for case 1>
...
```

### Attempt Records (`attempts.txt`)
```text
version 1
attempts <N>
<task_id> <attempt_num> <first_attempt> <compiled_rc> <verified_flag> <wall_ms> <prompt_tokens> <completion_tokens> <total_tokens>
```
Field order:
1. `task_id`: Identifier of the benchmark task (`task01`..`task20`)
2. `attempt_num`: 1-based attempt sequence number
3. `first_attempt`: `1` if first attempt, `0` otherwise
4. `compiled_rc`: Return code from `omc` (`0` = success, `>0` = compile error code)
5. `verified_flag`: `1` if all behavior cases passed, `0` otherwise
6. `wall_ms`: Milliseconds elapsed during compilation and execution
7. `prompt_tokens`: Input tokens consumed
8. `completion_tokens`: Output tokens generated
9. `total_tokens`: Sum of prompt and completion tokens

Rejection:
- First line must be `version 1` (exit 3 on mismatch).
- Line count must match `attempts <N>` (exit 2 on truncated or extra data).
- Non-numeric or invalid fields exit 2.

### Summary Report (`summary.txt`)
Emitted by `scorer.om`:
```text
version 1
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
verified_per_1k_tokens <fixed3>
verified_per_minute <fixed3>
status PASS|FAIL
```

## 2. Tasks

The held-out benchmark contains 20 algorithmic and bitwise tasks implemented and checked in native `.om`:
1. `task01`: `clamp_add_u16` -- Saturated 16-bit addition
2. `task02`: `absdiff_i32` -- Exact absolute difference $|a - b|$ returning `ulong`
3. `task03`: `popcount_u32` -- Count of set bits in 32-bit unsigned integer
4. `task04`: `gcd_u32` -- Greatest common divisor via Euclidean algorithm
5. `task05`: `isqrt_u32` -- Integer square root $\lfloor\sqrt{x}\rfloor$
6. `task06`: `collatz_steps_u32` -- Steps to reach 1 under $3n + 1$ dynamics
7. `task07`: `bitrev_u16` -- 16-bit reversal
8. `task08`: `fibonacci_u32` -- $n$-th Fibonacci number mod $2^{32}$
9. `task09`: `factorial_u32` -- $n!$ mod $2^{32}$
10. `task10`: `is_prime_u32` -- Primality test
11. `task11`: `parity_u32` -- Bitwise parity (1 if odd, 0 if even)
12. `task12`: `clz_u32` -- Count leading zero bits
13. `task13`: `ctz_u32` -- Count trailing zero bits
14. `task14`: `power_mod_u32` -- Modular exponentiation $(a^b) \bmod 2^{32}$
15. `task15`: `min3_i32` -- Minimum of three signed 32-bit integers
16. `task16`: `max3_i32` -- Maximum of three signed 32-bit integers
17. `task17`: `sum_digits_u32` -- Sum of decimal digits
18. `task18`: `reverse_digits_u32` -- Reverse decimal digits
19. `task19`: `hamming_dist_u32` -- Hamming distance between two 32-bit integers
20. `task20`: `rot_left_u32` -- 32-bit circular left rotation

## 3. Usage

Run self-test (verifies scorer compilation, positive control reference replay, negative control mutant catch, and format rejection):
```bash
./bench/inference_cost/driver.sh --self-test
```

Replay reference solutions:
```bash
./bench/inference_cost/driver.sh --replay-ref
```

Replay mutant set (negative control):
```bash
./bench/inference_cost/driver.sh --replay-mutants
```
