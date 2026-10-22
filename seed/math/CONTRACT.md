# Native exact math v1, L494

This is an independently written Omega implementation under the repository's AGPL-3.0 licence. The executable contains native AArch64 instructions and fixed pools. No interpreter, C/Rust/Python math library, Wolfram engine, service, heap, or floating-point arithmetic is in the math path.

Build from the repository root with `quiet-build sh seed/math/build.sh`. Run `build/math-native/omega-math`, supplying one request per line on standard input. It emits one result per request. Example: `printf 'expand (x+1)^5\n' | build/math-native/omega-math`.

| Request | Meaning |
|---|---|
| `eval E` / `expand E` / `collect E` | The same canonical expanded polynomial |
| `diff E` | Derivative with respect to x |
| `integ E` | Polynomial antiderivative, constant fixed to zero |
| `subst E ; R` | Substitute the exact rational R for x |
| `solve L ; R` | Solve the single linear equation L=R; returns x's value |
| `gcd A ; B` | Monic polynomial GCD over Q; gcd(0,0)=0 |
| `linear a;b;c;d;e;f` | Solve a*x+b*y=e and c*x+d*y=f; returns x;y |

Grammar: decimal integer literals, the variable `x`, parentheses, binary `+ - * / ^`, and unary `+ -`. Spaces, tabs, and CR are whitespace. Multiplication is explicit. Powers associate right; unary signs bind below powers. Exponents must evaluate to integers of magnitude <=1024; negative powers require a scalar base. Symbolic denominators are refused even if their numerator is zero or identical. Literal `0^0` refuses; the formal polynomial `x^0` is 1. No implicit decimal/float conversion, functions, units, extra variables, noninteger powers, or symbolic assumptions are accepted.

Canonical output starts `OK ` followed by ascending nonzero terms, reduced rational coefficients, positive denominators, and no spaces. Zero is `0`, coefficient 1 is omitted on x terms. Negative signs belong to numerators. An integration result denotes one antiderivative, with arbitrary constant omitted by contract.

The domain is Q[x], also valid as polynomial identities over characteristic-zero real/complex extensions. Declared state `domain=1`, `variable=120` (ASCII x), `assume=0` (no restrictions) is reset on each request. Two-variable linear solving uses a separate matrix contract with rational unknowns x,y. No rewrite turns x/x into 1, sqrt(x^2) into x, or float reassociation into exact equality.

| Status | Meaning |
|---|---|
| `REFUSED 1` | Malformed input, invalid bytes, missing separator, or trailing input |
| `REFUSED 2` | Zero denominator, zero negative power, or 0^0 |
| `REFUSED 3` | Unsupported operation, variable, exponent, or domain |
| `REFUSED 4` | Integer, degree, exponent, input, or output capacity |
| `REFUSED 5` | Rational or polynomial node arena exhausted |
| `REFUSED 6` | Explicit operation fuel or parser depth exhausted |
| `REFUSED 7` | Underdetermined linear equation/system |
| `REFUSED 8` | Inconsistent linear equation/system |
| `REFUSED 9` | Internal arithmetic invariant failure |

Limits are explicit: 32 limbs of base 10000 (128 decimal digits per magnitude), degree <=16, 4096 rational slots (18 reserved), 512 polynomial nodes (3 reserved), 4095 bytes per request, 8192 output bytes, parser nesting <32, and 200000 fuel units. The arithmetic bound applies to intermediate magnitudes too, so a mathematically representable final result can still be refused. Products cross-cancel before multiplying. All loops also retain the seed compiler's own safety bound. Numbers are exact within this bounded domain; this is not arbitrary precision or general integration.

The canonical structure is a bounded, interned polynomial-node table referencing immutable reduced rational nodes. Equal coefficient vectors share a polynomial handle within a request. It is a shallow canonical algebra graph, not a general expression DAG or a trace-preserving syntax tree. Node handles are private and request-local; the canonical text is the stable representation.

## Callable Omega module

`core.om` is a whole-unit source module without main or I/O. The checked unit is assembled from this source and one entry point, then compiled together by omc. The build shell performs file concatenation only, without source macros or code generation. The compiler checks the resulting entire unit. This seed compiler has no digest-import or shared-object export facility; neither is claimed here.

Within that unit, fill the declared `req: uchar[4096]`, then call `mathrun(n: ulong) -> ulong`. Only n bytes are read. A zero return publishes `res[0..reslen[0])`; a nonzero return leaves `reslen[0]=0`. Outputs are staged until parsing and arithmetic succeed; refusal never publishes a partial result. The backing output buffer is scratch and its bytes beyond reslen are unspecified. Every call resets request-owned arenas and state; callers must copy output before another call. Calls are serialized within this fixed workspace. Low-level routines and handles are private, not a foreign ABI. Host-safe independent workspaces and concurrent calls await compiler support.

## Integration freeze

The future `om math` and `omg math` adapters must pass this exact line protocol to the standalone binary or use the equivalent checked-unit `mathrun` contract. Both aliases must expose identical behavior and preserve REFUSED codes, domain and capacity information. They must never silently retry using approximate arithmetic or an external CAS. No omega-bridge files are changed, and these aliases are not hooked up by this milestone. This is also not a drop-in replacement for aien-calc's units, float formats, JSON receipts, or full grammar.

## Gates and remaining scope

Run `node seed/math/gate.mjs` after a successful build for the frozen initial vectors. Broader oracle, property, boundary and mutant gates accompany this milestone. Research provenance and actual runs are in `evidence/math-native/`.

Next milestones: wider or configurable bignum/rational capacity and improved addition cancellation; general expression DAGs with explicit domains/assumptions and guarded rewriting; broader polynomial solving and exact linear algebra; certified numeric intervals; then broader CAS functions and integration. No Wolfram-equivalence claim.

Transport limits: a process accepts at most 65536 requests, with at most 65536 bytes read for one line (including its newline). Inputs beyond the math input limit but within this transport limit yield REFUSED 4 and the next line can succeed. Reaching the transport byte bound yields REFUSED 4 then exit 1. Reaching the session request bound yields exit 3. I/O failures yield exit 2. EOF after a partial line evaluates that line; empty EOF yields no output. These explicit bounds avoid the seed compiler's implicit million-iteration trap. Math refusals on ordinary lines leave the CLI running with process exit zero at EOF; callers must inspect each result line.

Reproduce the math qualification with `sh seed/math/gate.sh`; use `--full` to add the original host compiler/checker and seed rung suites. Test-only dependencies are Node (independent BigInt reference and orchestration), aien-calc (oracle), standard shell tools, and the existing legacy-suite toolchains. `bootstrap.sh` can build the compiler from source without the external qualified executable. The build first validates compiler status, nonempty AArch64 ELF, and the pinned hash, then executes it. Build outputs stay under build/math-native and are not committed.

Command separators use the same whitespace set as expressions: space (ASCII 32),
tab (9), and CR (13). LF terminates a transport request. `eval\r1` and CRLF requests
therefore succeed; missing separators, vertical tab/form feed, and trailing tokens
remain refused. `whitespace.mjs` exercises all command boundaries, EOF, exact
input limits, CRLF, and recovery after malformed/domain/oversized requests.

`oracle.mjs` keeps the 400 generated rational cases and frozen magnitude RNG
consumption, with operand signs explicitly cycling `++`, `+-`, `-+`, `--`.
Coverage is counted from executed request text: 100 cases per sign pair and 25
per pair per arithmetic operation. Outputs must match independent BigInt direct
cross-products and have positive denominators, reduced values, and canonical
signs. `sign-fixtures.mjs` separately covers signed denominators, zero numerators,
zero results, zero divisors, and undefined zero powers. The reference suite still
executes all 1048 cases including nine supplied Wolfram fixtures.
