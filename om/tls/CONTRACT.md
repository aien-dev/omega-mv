# om/tls contract (OmegaTLS lane, 2026-10-26)

Goal: a TLS 1.3 client in seeded Omega, as source library units in aien-dev/omega-mv `om/tls/`.
Consumers: catalog.om (HTTPS GET api.github.com, raw.githubusercontent.com), omega_projects_pusher
(api.github.com), allen_relay_mailer (SMTPS mail.gandi.net:465).

## Toolchain (pinned; never rebuild it, never edit compiler source)
- Source: omega-mv main 19f014f2178ca9cd3473728fe00a475a2398c409, clone at /tmp/omtls (shared tree;
  write ONLY your own files listed in your task; no git commands that change state there).
- Compiler: /tmp/omtls/seed/out/omc, sha256 befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d.
  Check the hash before trusting results. Compile: `omc < unit.om > bin 2> bin.err`; rc 0 + empty
  stderr = accepted. Never run output of a failed compile. Runtime trap = exit 100+code.
- Language reference: /tmp/omtls/docs/one_layer/OMEGESE.md sections 3, 9, 9.13 (OM-2), 10 (host words).
  Read it before writing code. Fixtures: /tmp/omtls/seed/omc/om2/. Prior-art crypto in .om (OM-1 style,
  read-only reference for idioms): /tmp/omnet-ro/om/sha512/sha512.om, /tmp/omnet-ro/om/ed25519/ed25519.om
  and their test/run.sh + driver.om (vector-driver pattern to copy).
- HOST RULE: every build/test command runs under `nice -n 19 ionice -c3`; at most 2 parallel jobs.
- Math rule: any computed constant (curve params, Montgomery constants, primality, etc.) is either taken
  verbatim from the standard (cite section) or computed at run time in .om, or checked with the
  Wolfram MCP tool (write JSON to xd://mcp__wolfram_wolframlanguageevaluator), quoting the query.
- No Signed-off-by. No host-language implementation of any primitive: Python is allowed ONLY as
  test tooling (vector converters from Wycheproof/NIST JSON/rsp to flat txt, differential oracles).

## Language facts that bite (verified on this omc)
- Unit is OM-2: the build writes a single line `om2` first, then concatenates the library files, then
  the program/driver. Library files themselves must NOT contain an `om2` line and must NOT have `fn main`.
- OM-2 gives: unsized params `a view uchar *` / `a edit ulong *` (len a = run-time count), `part A o k`
  as an argument to an unsized param, hex literals `0xff` (lowercase), `rotr`/`rotl` on unsigned,
  `for i T K unroll` (K<=64), `fn ... to T inline`.
- PARAMETER SLOTS: at most 6 per function; an unsized array costs 2 slots, a scalar or sized array 1.
  So a function takes at most 3 unsized arrays. Pack small fixed inputs into one array.
- At most 16 live registers per function (error 12: split the function). Names other than functions:
  at most 8 chars. Function names up to 63 chars. A new local may not equal ANY function, global,
  const, text or struct name in the composed unit (error 10), nor a keyword/host word (send recv
  connect bind listen accept random stat link fork read write len part out open seal field at ...).
- No implicit conversions; `ulong <bool>` is refused (error 9): build constant-time masks
  arithmetically (e.g. m = sub 0 bit, eq-zero via ((d | (0-d)) >> 63) ^ 1).
- Globals cannot be passed as arguments (error 19). `new T N v` locals only in a function's outermost
  block. Unsigned arithmetic wraps; signed traps on overflow. No 64x64->128 multiply: use 32-bit limbs
  in ulong (or 16/26/51-bit radix as you choose) so products never exceed 2^64.
- Every loop is bounded (`while c bound N`, N<=999999; `for` traps after 1,000,000 passes).
- omc main limits per unit: 512 functions total (the WHOLE composed TLS client must fit, so keep your
  module lean: target the function budget in your task), 8192 trap sites per function.

## Naming (composition safety)
- Every library function name contains `_` and starts with the module prefix given in your task.
- Every const/text/global name is UPPERCASE and starts with your 2-letter module code.
- Locals/params: lowercase, no `_`, <= 8 chars.
- Library modules: no mutable globals (consts and texts are fine). Caller passes all workspace.

## Error policy
- Functions that judge untrusted input return ulong: 0 = success/valid, nonzero = a documented
  reject code (list them in your README section). Untrusted input must NEVER trap (check lengths
  and ranges before indexing). Traps are only for caller-contract violations (e.g. output too small).
- Constant time: any function handling secrets (keys, plaintext, ephemeral scalars, MAC tags before
  comparison) has no secret-dependent branch, loop count, or array index. Tag comparison is
  constant-time. Signature verification inputs are public (variable time allowed; say so).
  Wipe secret local arrays before returning where practical. Document exactly what is and is not
  constant time, including the compiler-level caveat (omc emits plain AArch64; we rely on MUL/ADD/
  EOR/shift being data-independent; no DIT bit is set).

## Tests (per module)
- Layout: om/tls/<module>.om, om/tls/test/<module>/{driver.om,run.sh,vectors/*,gen_*.py}.
- run.sh usage: `sh om/tls/test/<module>/run.sh /tmp/omtls/seed/out/omc`; builds into
  /tmp/omtls/build/tls/<module>/ (build/ is gitignored); prints PASS/FAIL lines and a final line
  `<MODULE> PASS vectors=N (wycheproof=W ...) mutants=M/M` or FAIL; exit 0 only if all pass.
- Vectors: committed flat-text files converted from the source, with a header recording source URL,
  source commit/sha256 and license (copy the upstream LICENSE/NOTICE beside them).
  Wycheproof clone (read-only): /tmp/wycheproof at commit d17cc89fa5c8f4f4d378b5385ebf75da5d0926c9,
  files in testvectors_v1/. Run every test of every listed file; the expected outcome is the
  Wycheproof `result` ("valid" must pass, "invalid" must be refused, "acceptable": record which way
  you go and why). Report exact counts: total / valid-pass / invalid-refused / acceptable split.
  Files whose algorithm we do not implement are listed as NOT_RUN with the reason.
- Mutants: at least one source mutant of the library per primitive (sed a real constant/operator in a
  copy of the .om), which the driver MUST detect (exit 1 with MISMATCH lines; a trap or a crash does
  not count as detection) — copy omnet's mutant() logic. Plus one expectation mutant.
- Also a negative control in the driver: flip one byte of a valid vector and require rejection.
