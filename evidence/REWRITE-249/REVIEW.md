# Independent review of REWRITE-249 (ledger L061)

Reviewer: Hermes coordinator session on claude-fable-5-1 (a different model from the builder, which ran on
claude-opus). Date 2026-10-19, 22:4xZ. Branch `research/rewrite-elim-249` at 0410739c. This is the
"different model reviews than built" step of #249 and the G6 reproduction of the checker's six named
cases; it is not a rerun of the timing of record.

## Method

1. Read `tools/rw249/contracts/axioms.rwa` and confirmed every law by hand in its declared domains:
   ring laws (add/mul assoc, comm, zero, one, neg, distrib, sub-def, neg-add) hold in Z and in Z/2^w;
   Boolean laws (and/or assoc, comm, ones, zero, both distributive laws) hold on w-bit words;
   `isqrt-square` holds in Z only with p >= 0; `mul-div-cancel` holds in Z with q != 0 because p*q is an
   exact multiple of q, so truncating division is exact for either sign.
2. Re-derived the three accepted contracts step by step from the contract files alone, before running
   any tool:
   - R18 `(mul (add x a) m) -> (add (mul x m) (mul a m))`: one instance of `mul-distrib` with p=x, q=a,
     r=m. Valid in wrap at every width. AGREE proven_in_scope.
   - R22 `(or (and x m) c) -> (or x c)` under `(or m c) = ones`: step 1 `or-distrib` with p=x, q=m, r=c
     gives `(and (or x c) (or m c))`; step 2 substitutes the assumption; step 3 `and-ones`. Each step is
     one valid rewrite. Spot value: m=0xF0, c=0x0F, x=0x33 gives 0x3F on both sides. AGREE proven_in_scope.
   - R02 `(sub (sub x a) b) -> (sub x (add a b))`: sub-def (outer), sub-def (inner), add-assoc, neg-add,
     sub-def reversed. Each step matches its axiom exactly. AGREE proven_in_scope.
3. Re-derived the three rejections:
   - N01 `(isqrt (mul x x)) -> x` in Z with no assumption: the cited `isqrt-square` carries the side
     condition p >= 0, which the contract does not supply, so the step is invalid. Counterexample x = -1:
     isqrt(1) = 1 != -1. AGREE rejected.
   - N03 `(udiv (mul x 2) 2) -> x` in wrapping u32: `mul-div-cancel` is declared for the int domain only;
     in wrap the step is invalid. Counterexample x = 2^31: x*2 wraps to 0, 0/2 = 0 != 2^31. The contract's
     assumption `(ne 2 0)` is true and irrelevant; the defect is the domain. AGREE rejected.
   - N04 f32 `(add (add x a) b) -> (add x (add a b))`: `add-assoc` is declared for wrap and int, not f32.
     Counterexample x = 1, a = 1e8, b = -1e8 in binary32: 1e8 is exactly representable (2^8 * 390625,
     390625 < 2^24), the ulp at 1e8 is 8, so 1 + 1e8 rounds back to 1e8 and the left side is 0, while the
     right side is 1 + 0 = 1. AGREE rejected.
4. Then built the checker in a private output dir and ran the six commands from the handoff section
   "For the independent reviewer" (build with `nice -n 19 ionice -c3 make ... rwcheck`, no quiet lock:
   this is a correctness check, not a timing; three GPU measurements had been queued on the quiet lock
   since 17:01Z).

## Checker output (tool agrees with the hand derivation in all six cases)

| contract | expected | rwcheck result | detail |
|---|---|---|---|
| R18 | proven_in_scope | proven_in_scope | 1 step; guard rows 20,185,088, skipped 0 |
| R22 | proven_in_scope | proven_in_scope | 3 steps; guard rows 1,941,760 checked, 18,243,328 skipped where the assumption fails |
| R02 | proven_in_scope | proven_in_scope | 5 steps; guard rows 20,185,088, skipped 0 |
| N01 | rejected | rejected | counterexample bad_rows 128 of 256 (the negative half of the int window); step 1 invalid: side condition of isqrt-square not in the assumptions |
| N03 | rejected | rejected | counterexample bad_rows 1,703,936 of 3,407,872 (exactly half: every x >= 2^31); step 1 invalid: mul-div-cancel not valid in wrap |
| N04 | rejected | rejected | counterexample bad_rows 493 of 67,264 sampled; step 1 invalid: add-assoc not valid in f32 |

Semantics tag reported by the tool: omega.v0.wrap.1 for every case (N01 and N04 are checker-only
domains, as their contracts state).

## Verdict

G6 (independent reproduction of the checker's accepted and rejected cases): PASS, 6 of 6 agree, by a
different model from the builder, from the contract files alone, then confirmed by the tool.

Not covered by this review: the timing of record (search and check CPU per arm, the FAIL on net saving
at 10^6 executions) was not rerun; the held-out expression set was not re-searched; the claim that the
checker is "independent" rests on it sharing no search code, which I read in the tree layout
(`tools/rw249/rwcheck.c` separate from `rwsearch.c`) but did not audit line by line.

## Two observations for the builder (not defects)

- R18 is labelled `count-neutral` although its right side has one more operation than its left; the
  label presumably refers to the cost model's view, and the README should say which.
- N03's assumption line `(ne 2 0)` is satisfied and plays no role in the rejection; a reader may think
  the assumption is what failed. A one-line comment in the contract would prevent that.
