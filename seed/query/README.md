# seed/query: omq, the hole oracle for agents writing seeded Omega

`omq` answers one question an agent writing `.om` keeps asking: what may I write here? It reads a
program with one hole `??` and prints, as one JSON line, the names in scope with their types, the
functions, every statement or value form that fits the hole, the open obligations (blocks to close,
names the register pool still allows, contracts, effects) and the diagnostic with a repair when
the program does not compile.

`omq` is written in seeded Omega. It does not reimplement the compiler's rules. It runs the pinned
`omc` as a subprocess on witness programs (memfd input and stderr, `fork`, `execprog`, `waitpid`)
and reports only what `omc` accepted. A form appears in `next` only after a witness with that form
at the hole compiled. When a rule changes in `omc`, the answers change with it; nothing in `omq`
needs editing.

Status: EXERCISED on the host (Linux AArch64, DGX Spark, 2026-10-26) against compiler
`2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f` (omega-mv `0b77e6ea`).
`check.sh` printed `SEED QUERY PASS 45 checks`. This is host evidence only. It is not a merge, and
no agent-loop result is claimed here.

| File | Contents |
|---|---|
| `omq.om` | The tool (one OM-1 program unit) |
| `check.sh` | Gate: goldens, negative controls, recompiled next forms, fake compilers, a mutant |
| `cases/*.om`, `cases/*.json` | Inputs and reviewed expected answers |

## Build and use

```sh
seed/out/omc < seed/query/omq.om > omq && chmod +x omq
./omq seed/out/omc < program.om          # one JSON line on stdout
./omq seed/out/omc prefix                # prefix-viability server (below)
sh seed/query/check.sh                   # PASS / FAIL / NOT_RUN (wrong compiler)
```

The first argument is the compiler. `omq` refuses one that fails its selftest (a known-good
program must compile and a known-bad one must not), so `/bin/true` or a wrong path gives exit 2,
never an answer.

Hole syntax: a line that is only `??` is a statement hole; `??` inside a line is a value hole. With
no hole, `omq` only checks the program. Exit codes: 0 answer printed, 1 more than one hole or a
hole outside a function (the answer says which), 2 usage or a compiler that fails the selftest.

## Answer (abridged from `cases/stmt_loop.json`)

```json
{"hole":{"line":5,"kind":"statement","fn_line":2,"depth":2},
 "check":{"code":0},
 "scope":[{"name":"cnt","type":"ulong[]","writable":true},{"name":"n","type":"ulong"}],
 "fns":["fn sum a view uint 8 n ulong to ulong"],
 "next":["ret <ulong>","n = <ulong>","NEW = <value>","if <bool>","break","put cnt <ulong> <ulong>"],
 "obligations":{"function":"fn sum a view uint 8 n ulong to ulong","open":["for","fn"],
   "regs_free":11,"contracts":["requires le n 8"],"effects":["write:io"]},
 "compiles":70}
```

- `hole`: 1-based line, `statement` or `value`, the enclosing function's line, block depth.
- `check`: `code` 0 when the text before the hole, closed off by a trap and the missing `end`s,
  compiles. The text after the hole is not checked: it is the part still to be written. Otherwise
  the `omc` error `code` and `line`, and for an OM-2 diagnostic `sev`, `col`, `expect`, `found`,
  `obligation`, `span`. `why` is a sentence; `fix_rename`, `fix_move_up` or `fix_append_end` is a
  repair that `omq` recompiled, with `rc_after_fix`.
- `check.status`, one of four values and never a plain VALID:
  - `INVALID`: `omc` rejects the text, and with a hole the error lies before it, so nothing written
    at the hole can repair it;
  - `INCOMPLETE`: with a hole, the closed-off witness compiles, so a valid completion exists;
  - `VALID-UNPROVEN`: no hole and `omc` accepts the program. Well formed only: contracts become
    run-time traps and no behaviour is proven;
  - `UNKNOWN`: with a hole, the error lies after it in the witness's added closing lines and may
    depend on what the hole holds.
- `scope`: names visible at the hole, typed by probing `omc` (an array is `T[]`; `writable` when
  `put` into it compiles; a fresh array, whose elements may not be read before a `put`, is typed
  by writing element 0 first; `struct` when `get NAME F` compiles for the first field `F` of a
  declared struct). Candidates are parameters, `=` bindings, `for` variables and bare `NAME VALUE`
  bindings (`pu s`), which `omc` accepts at the pin. A variable named `export` is a name, not a
  function line. Names from closed blocks are absent.
- `next`: statement forms (or, at a value hole, values and operators of the expected type in
  `expect`) that compiled at the hole. `<T>` is a slot for a value of type T. A call to a user
  function is shown with a concrete argument list: a typed literal per scalar parameter and a
  visible array per array parameter (the first of up to four candidates `omc` accepts).
- `obligations`: open blocks innermost first, how many more `ulong` names fit before error 12
  (error 14, capacity, does not end the count; an `inline` function is probed as a plain one, so
  the count is its body's, not a call site's expansion),
  the function's `requires`/`ensures` and the invariants of loops still open, and the host-word
  effects the function already uses (`read:fs`, `write:io`, `net`, ...; the table covers every
  host keyword of `omc`, which `check.sh` checks).
- `compiles`: how many times `omc` ran.

## Prefix mode

`omq OMC prefix` serves requests on fd 0: a 4-byte little-endian length, then that many bytes of
program prefix. For each it writes one line `V CODE`: whether the prefix can still be completed to
a program `omc` accepts, and the code. This replaces `omviable.om` (OmegaMind, handoffs
`OmegaMind/omviable/`), which this unit absorbed; OmegaMind's own `selftest.py` passed 13/13 cases
against it. An open word at the prefix end that is longer than any word `omc` accepts (a name over
`NAMEMAX` 8 bytes, a number over `NUMMAX` 20 bytes) is answered `2`, since growing it never
helps; before this, `local arr[i324i324` was answered `1 13` and sampled models ran to max_tokens
(OmegaMind, `OmegaMind/omviable/wdead-check.txt`). Note lines, text literals and prefixes of
`invariant` are exempt; a first word that merely starts with `note` is wrongly exempt (fails open).
#837 raises `NAMEMAX` to 63.

## Limits

- One hole per request.
- A call with several array parameters gives every array parameter the same candidate rank, so a
  call that needs two different arrays of one element type may be missing from `next`
  (`compress s w kt m o` in `om/sha256/sha256-om2.om`).
- Each answer costs tens to a few hundred compiles; at 1000+ line programs one answer took up to
  0.7 s on one X925 core.
- Struct-typed parameters are not filled.
- An abstract value is in `scope` with an empty type: no probe names its type.
- Scope order is first mention, so a loop variable reused after a closed loop of the same name
  can come before names declared between the two loops.
- A hole inside an `inline` function whose caller passes a `fresh` array gives `check` code 27:
  the trap at the hole leaves the callee's writes out of the caller's expansion.

## Evidence

`check.sh` on 2026-10-26: `SEED QUERY PASS 45 checks`, `omq` sha256
`62a21f4462e217f4efcc3eaf1495ca65a0b51a7d47cb1dac25e54a92e1877f9c`. The checks are byte-identical
goldens for every case; negative controls (closed-block name absent, no `break` outside a loop, a
wrong-type array excluded, no contract forms in OM-1, a length-mismatched array not offered and
rejected by `omc`); every `next` form at the loop hole substituted and recompiled; the omitted
`else` refused; the error-13 repair recompiled; three fake compilers refused; prefix verdicts; a
corrupted golden detected; a mutant `omq` that accepts every witness caught.

Real programs (harness `trial.py`, handoffs `OmegaIntent/realprog/`, pinned to cpu 17, X925): a
hole placed at a seeded random statement line of standalone repo programs (BLAKE3, SHA-256, the
WASM and SPIR-V emitters, the OM-2 gate, host tests). With the `omq` above (sha `b7fcd8a4…`), on
a sample drawn after the last scope change (seed 4242, 112 holes in 28 programs), every base
witness compiled and the removed statement's form was in `next` for 112 of 112; the 95%
Clopper-Pearson lower bound is 0.968
(`WolframLanguageEvaluator: InverseBetaRegularized[0.025, 112, 1] -> 0.9676`). Median wall time
per answer 0.031 s, 90th percentile 0.26 s, maximum 0.69 s, all on cpu 17 (X925)
(`WolframLanguageEvaluator: {Median[w], Quantile[w,0.9], Max[w]} -> {0.031195, 0.26379, 0.68583}`;
data `realprog/fresh4.jsonl`).

White-box differential (omega-mv #843, `om2 query` in `omc`, binary sha256 `2c3124f6…`): 827
statement holes, up to three per tracked `.om` file that compiles (seed 838, 393 files). Comparing
`hole`, `scope`, `fns` and `obligations`, 625 of 827 answers are equal (525 before this change;
`WolframLanguageEvaluator: N[{625/827, 525/827}] -> {0.7557, 0.6348}`). Of the rest, 169 differ only
in `effects` (`omq` lists every host word use in the function; `omc`'s list is duplicate-free and
a subset of it in all 825 answered holes), 2 have no white-box answer (`omc` error 16), and 31
differ in `scope` (6 of them also in `effects`, 1 also in `regs_free`): the three limits above
(16 abstract, 14 order, 1 fresh inline).
