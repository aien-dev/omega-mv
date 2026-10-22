# L494 research and reuse

Read the L494 brief in full, the B-to-C/bootstrap handoffs, and language-study decisions 26-45 and compatibility border design from branch study/languages. The proposed foreign ABI work is not present in the pinned seed compiler, so the deliverable is a callable whole-unit Omega module plus standalone executable.

Source checkout starts at 29b12760f4c962606411d6bf25032aedef063301 on feat/native-symbolic-math. Remote omega-mv/main was a8ffd2b912c84197d08f963b05f9c7d8b30a3f8a when inspected. This deliberately pinned seed branch is not claimed to be current merged math behavior. The original worktree remote points to aien-spark/omega; publication uses explicit aien-dev/omega-mv destination as requested.

Inspected actual `omega-symbolic/src/runtime/rx_symbolic.h` at the symbolic/rewrite-layer worktree and its typed WRAP/RNE/BLOCK and assumption-aware design. Its C optimizer is not a native CAS. Reuse is conceptual: bounded work, explicit arithmetic domain, guarded identities, canonical expressions. No C implementation is copied or linked.

Inspected actual sc-calc/crates/aien-calc sources (value.rs, ratutil.rs) and cut1_eval.rs vectors; aien-calc uses BigRational/BigInt and supports units, receipts, and format operations. The historical design statement that no calculator exists is obsolete. Its CLI is available and retained solely as a test oracle. This native milestone does not claim full calc-lang or receipt compatibility. Searched seed sources and the study branch for bignum/rational primitives; no reusable native multi-limb implementation was found there.

Public sources consulted on 2026-10-22:

- https://www.wolfram.com/engine/faq/ explicitly says the engine is not open source. Its separate licensing is another reason it is not embedded here.
- https://reference.wolfram.com/language/tutorial/SomeNotesOnInternalImplementation.html describes expression representation and broad internal techniques; it is not kernel source and no kernel reconstruction is attempted.
- https://docs.sympy.org/latest/modules/polys/domainsintro.html distinguishes Q, Q[x], and rational function fields. The implemented domain is Q[x]; rational-function cancellation and its excluded points are outside scope.
- https://docs.sympy.org/latest/modules/polys/basics.html documents polynomial expansion, division and Euclidean GCD. The implementation independently uses coefficient convolution, leading-term remainder, and monic Euclid over Q.
- https://dlmf.nist.gov/1.4 supplies calculus definitions and differentiation/integration identities. Polynomial calculus here directly applies the power rule to exact coefficients. Rational substitution uses Horner evaluation.
- Rational canonicalization uses elementary integer Euclid to remove gcd and keeps the denominator positive. Base-10000 schoolbook multiplication and long division with a binary search per quotient digit are independently implemented elementary positional arithmetic. GMP manual fetch was attempted but unavailable; no claim of reading its implementation is made.

`wolfram-oracle.json` is copied verbatim from the parent-provided actual-output fixture, preserving its provenance string. Tests map its nine results to this CLI grammar and canonical presentation. No outputs are invented or attributed to a new Wolfram call. No commercial disassembly, library copying or runtime delegation.

Follow-up inspection included rx_symbolic.c interning/assumption construction and rx_symbolic_cache.c. That cache stores verified rewrite results in the existing semantic engine and allocates C heap state; it cannot be linked into this no-heap native path. The native module uses fixed request-local storage and exposes canonical text for later semantic-object integration. A content search across all local branches' .om and .omegese files found no pre-existing bignum/rational implementation (only the new module); raw results are in native-reuse-search.log.
