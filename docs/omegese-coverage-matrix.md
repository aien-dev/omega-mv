# Omegese / seeded compiler coverage matrix (#625, ledger L839)

Audit only. Baseline: omega-mv `main` at `acb3f15cdc691b1b046b48b0dad0f9255a12ba4b`, compiler
`seed/omc/omc.om` (7838 lines), built `seed/out/omc` sha256 `734f0a39…6b61`. omega-bridge
`origin/main` at `012860db`. Inspected 2026-10-25. Candidate branches are listed as **candidate**
and are not on main unless the row says so (ancestry checked with `git merge-base --is-ancestor`).

## Status vocabulary

- **SUPPORTED**: implemented in source and exercised by a named gate or test.
- **QUALIFIED**: an independent review or qualification receipt exists; the link is given.
- **UNSUPPORTED**: absent and refused with a named compiler error, or absent and stated absent.
- **UNKNOWN**: inspection did not establish the status.
- *doc-only*: the claim appears only in documentation; no source or gate was found behind it.

## Abbreviations

- `omc.om`: `seed/omc/omc.om`.
- `OMG`: `docs/one_layer/OMEGESE.md`.
- `OMC-DONE`: `docs/one_layer/OM_COMPLETION.md`.
- **OMEGA-SEED**: `tests/gates/omega-seed.gate`, which runs `tests/gates/run.sh seed`, which runs
  `seed/host/check.sh`. That script runs `gate-ir.sh`, `gate7`–`gate18`, and `seed/host/{gate,negative,regression}.sh`.
- **Not wired**: OMEGA-SEED does **not** run `seed/omc/gate5.sh` or `seed/omc/gate6.sh`.
  - `gate5.sh` reruns `seed/rung4/gate4.sh` with omc substituted (`gate5.sh:70`).
  - `seed/omc/qualify-ir-trap.sh:25,31` runs gate6, and gate5 sections 1–6b, by hand.
  - `seed/qualification/security673/gate669.om` is not referenced from `tests/gates/` either.
- `P:` marks an ad-hoc probe with the pinned omc `734f0a39` (Appendix A). A probe is evidence but not a gate.
- Ledger rows come from `~/handoffs/LEDGER.tsv`:

| Ledger | Owner, scope | State |
|---|---|---|
| L408 | cc-omega-in-omega, compiler owner | open |
| L834 | om2-compiler, acting omc.om writer for #655/#745/#746/#665 | open |
| L835 | #599/#625 language-evolution coordinator | open |
| L836 | om2-qualification | open |
| L837 | om2-abi | open |
| L838 | om2-sha | open |
| L569 / L575 | #625 semantics v1 | open |
| L729 | recursion, #645 | open |
| L789 | security673 | open |
| L830 | #624 coordinator | open |
| L832 | #621 codec v2 | open |
| L608 | #622 C exporter | open |
| L615 | macOS port | done |
| L624 | macOS publication | open |
| L801 | x86-64, FR-9 | open |
| L184 | #601 kernel words | open |
| L117 | sm_121 rung 6 | done |
| L775 | #652 lead, orders #655 | open |
| L798 | own/free documentation drift | unassigned |

## Matrix

| # | Area | Status @ main | Owner (issue / ledger) | Evidence | Gap / next owner |
|---|---|---|---|---|---|
| 1a | Unsigned wrap (u8/u16/u32/u64) | SUPPORTED | #625 L835; compiler L408 | OMG:431; `gate14.sh:45` (`u64_shl63_hi`), Wolfram-derived expectations; OMEGA-SEED | #625 v1 contract has no seeded-omc profile (row 14). L835 |
| 1b | Signed overflow trap 1 (incl. MIN/-1) | SUPPORTED | #625 L835; L408 | `omc.om:1963-1966` (`bo_div`: CMN/B.VS trap 1); `gate14.sh:58,62,64` (`byte_add_ovf`, `int_add_ovf`, `int_min_div` → 101) | — |
| 1c | div/rem by 0 → trap 2; signed rem sign | SUPPORTED | L408 | `omc.om:1963` (CBZ trap 2); `gate14.sh:70` `div0`→102, `:51` `i32_min_rem_neg1`→0 | — |
| 1d | Shift count ∉ 0..w-1 → trap 7 | SUPPORTED | L408 | `omc.om:1914-1917` (`bo_schk`); `gate14.sh:69` `shift8`→107 | Immediate-shift path not separately audited |
| 1e | Explicit conversions; trap 5; no implicit | SUPPORTED | L408 | OMG:439-441; `gate14.sh:66` `conv_300`→105; `gate15.sh:33` e7 mixed types → error 7 | — |
| 1f | Decimal literals; error 8 out of range, error 6 malformed | SUPPORTED | L408 | `omc.om:171-216` (`tok_num`, `fail 8`); `gate15.sh:32,34` (e6, e8) | — |
| 1g | Hex literals | UNSUPPORTED (error 6) | #745 / L834 (+L836 qualification) | `tok_num` is decimal-only (`omc.om:171`); P: `uint 0x10` → `error 6` | #745 versioned hex syntax. L834 |
| 2 | Floating point f32/f64, NaN, rounding | SUPPORTED (AArch64 only) | L408; #625 | OMG §9.9 (543-580); `omc.om:856-1107` (exact decimal→binary, round-to-nearest-even, error 8 on ∞); `gate8.sh:55-64` (bits, arith, compare, convert, intrinsics), `:74-76` overflow refusals; `seed/omc/tests/float/compare.om:1` (NaN unordered); OMC-DONE:65 | IR mode refuses floats (error 3, OMG:579-580). No FP profile in #625 v1 (C profile refuses FP). Owners #625 / L835 |
| 3 | Vector / SIMD semantics | UNSUPPORTED (no word; generic error 3) | none found (row C1) | Keyword table `seed/omc/genkw.awk:16` has no vector type or word; `omc.om` has no NEON emission (only "vector" = env vector, `:3407`); P: `vadd` → `error 3` | No issue owns CPU SIMD in .om; #625 lists vector semantics as spec scope only |
| 4a | Initialization of storage | SUPPORTED | #746 / L834 | Scalars exist only after assignment (no declaration form). `new T N v` fills every element (`omc.om:4918-4919`; OMG:460). Globals are zero at start (`omc.om:5377`; OMG:454). `new S v1..vn` takes one value per field (`gate6.sh` `missing_val` → 4). Gate cases: `gate16.sh:41` | Logical-range initialization (partly written scratch) is not tracked. #746 |
| 4b | Definite assignment across branches | SUPPORTED by scoping; no dedicated gate | #746 / L834 | OMG:34-36 (a name born in a block dies at `end`; *doc*). P: write only inside `if`, read after → `error 3` | No named negative case found in gate15/16 for a branch-only write. #746 negative controls (L834, L836) |
| 5a | Allocation: stack arrays and structs, fixed pools; no heap (decision 24) | SUPPORTED | L408 | `omc.om:5` (compiler has no heap), `:6172-6173`; caps: `omc.om:4931-4933` 4 MiB → error 23 (`gate15.sh:47`); stack floor `omc.om:1707` `stkchk` trap 6 | — |
| 5b | `free` / `own` words | UNSUPPORTED (error 9) | L798 **unassigned** | `omc.om:4709-4711`, `:5993-5995` (`own`/`free` → `fail 9`); `free`: `gate6.sh:89` `still_free` → 9; `own`: P only, no gate case | L798's text is stale: `break`/`continue`/`trap` are implemented (`gate7.sh:55,65,67`; OMG §9.8). Assign L798 and add a gate case for `own` (row C4) |
| 5c | Allocation failure / cleanup | SUPPORTED (bounded) | L408; #636 | Stack exhaustion traps 6 before writing: `seed/rung4/tests/t_stack.want`=106, checked in `gate4.sh:91-94`, run with omc by gate5 (**not wired**); P: depth-1e8 recursion exits 106. Native mapping table, 128 slots (`omc.om:3853-3855`; `seed/host/gate.sh:53-61` `mapping`/`mapio`/`reserve`/`memcheck`) | No user cleanup/finalizer construct. Lifetimes are lexical (stack slot released at `ret`, `omc.om:1715`). Legacy C checker2 ownership (#465, PR #500) is not seeded .om |
| 6a | Aliasing: one array or struct passed twice → error 19; globals never passed | SUPPORTED (globals); implemented (same name) | #746 / L834 | `omc.om:2847-2852` (`ca_arr`), `:6674-6676` (`ca_dist`); `gate15.sh:43` `e19_global_passed`; P: `g q q` → `error 19` | No named gate case for the same array or struct passed twice. #746 aliased-write negative control |
| 6b | Struct layout: 8-byte cells, no padding; `sizeof`/`offsetof` | SUPPORTED | L408 | `omc.om:6172-6174`, `:1163-1166`; OMG §9.7, §9.11; `gate10.sh`; `gate6.sh` (26 cases, **not wired**) | — |
| 6c | External C layout (packed fields, endianness, SHA 104-byte ctx) | UNSUPPORTED (packed); SHA ctx held by a C assert | #746 / L834; L838 | OMC-DONE:63 FR-2 packed structs absent. `src/sha256.c:28` `_Static_assert(sizeof(sha256_ctx)==104)`. `om/sha256/sha256.om` reads bytes little-endian. ELF output is little-endian (`omc.om:5767`) | No .om layout contract. #746 named record layout (L834); SHA cutover L838 |
| 7a | Fixed-length `view`/`edit` parameters | SUPPORTED | L408 | OMG:464-470; `gate15.sh:44` e20 readonly → 20; `gate6.sh` `put_view` → 20 | — |
| 7b | Variable-length slices (.om and native ABI) | UNSUPPORTED (ABI status 32) | #747 / L837 (bridge), L838 (SHA) | omega-bridge `docs/NATIVE-ABI.md:58,62`: element count must equal the declared length, else status 32. `src/sha256.c:31` chunks in 4096-byte pieces; `om/sha256/sha256.om:13` `n <= 4096` | #747 slice ABI (L837; branch `feat/slice-abi` not yet pushed); SHA adapter removal L838 |
| 7c | Native call ABI v1 (function-mode units from C/C++/Python/Rust) | SUPPORTED | #747 / L837; bridge owner L483 | omega-bridge `012860db`: `docs/NATIVE-ABI.md:1-12,53-62,86-110`; `tests/native/run.sh` (BTI, overlap, null, misalignment, trap status 1..14, signal reentry 33). omega `tests/gates/omega-om-units.gate` (binding regenerated byte for byte, SHA/BLAKE3 parity; `run.sh:310`); `om/units.sh:8` pins omc. **Candidate:** draft bridge PR #14 `feat/native-bindings-main` @`3f059088` (six-language G1 calc) | ABI version negotiation/refusal of other versions not found (#747 item 5). No independent review on bridge PRs #12/#13/#14 (0 reviews). OMC-DONE:68 "FR-7 C ABI absent" predates or ignores ABI v1 (row D1) |
| 8 | Modules / linking / separate compilation | UNSUPPORTED (single unit; `import` → error 16) | #601 K5 `use` / L184; #636 FR-3 digest imports / L408 | P: `import sha` → `error 16`. OMC-DONE:64. Capacities error 14: `gate12.sh:45-49` (1024 names, 512 functions). Register pool error 12 (`gate15.sh` e12). Units link only as C binding includes (`om/units.sh`, one translation unit) | OSC IR mode (≤32 functions, earlier-only calls; OMG §11, `omc.om:6695`) is not a linker. #601/#636 |
| 9a | Traps (1..14), program exit 100+N, function mode `OscRt.trap` | SUPPORTED | L408 | OMG:406-407, 535-539; `gate14.sh` (16 trap cases); `gate7.sh:65` `t1`→105, `:67`; `seed/host/gate.sh` trap 3 → 103 | — |
| 9b | Exceptions / handlers | UNSUPPORTED (absent) | #625 (spec) | No catch/handler word in `genkw.awk:16`; a trap ends the program or calls the runtime (OMG:406-407) | Only an absence; no refusal code for handler syntax beyond generic errors |
| 9c | Effects / capabilities (least authority) | UNSUPPORTED | #670 / L789 | OMG:633-634 (host words run with the process's existing permissions; "do not form a sandbox"); host words are program-mode only, else error 9 (OMG:629) | #670 (L789) |
| 9d | Cancellation / deadlines | UNSUPPORTED in the language; process-level words SUPPORTED | #670/#673 L789 | `killpid`/`waitpid`/`waitio`/`sleepns` (OMG:683-697; `seed/host/gate.sh:27-28` `process`); deadline coordinator `seed/qualification/security673/gate669.om` (README:28-36, 30 s per child, **not wired**) | No language cancellation construct; #625 spec |
| 10a | Recursion: program mode self and mutual | SUPPORTED | L408; #645 L729 | OMG:415-417; `gate4.sh:81` (fib = gcc twin), `:158-160` (`r5b_mut`), run under omc by gate5 (**not wired**) | — |
| 10b | Recursion: function mode / IR mode | UNSUPPORTED (error 9 / error 3) | L408 | `omc.om:5826-5827` (call-graph cycle → error 9); `gate4.sh:122`; P: function-mode self-call → `error 9`. IR mode error 3 (OMG:760) | — |
| 10c | Fuel / logical recursion budget | UNSUPPORTED at main (stack trap 6 only) | #645/#646 L729 | No fuel in `omc.om`; only the stack floor (`:1707`) and `while` bound trap 4 (`gate14.sh:73` `bound`→104). **Candidate QUALIFIED:** `universal/645-bounded-recursion-L729` @`c434565c` (`spec/universal/bounded-recursion-v2.md`, `src/source-aware/recursion-v2.om`, `tests/universal/recursion/`). Independent review PASS (L736): `~/handoffs/omega-recursion-L729/independent-review.md`; [evidence](https://github.com/aien-dev/omega-mv/tree/c434565c67552c0b01df2d7f76c1a04287c70560/evidence/UNIVERSAL-645/L729) | Not merged. Its gate is `gate.mjs` (Node), not .om. #646 cache fuel accounting is open |
| 11a | Concurrency: CPU threads, atomics, memory ordering | UNSUPPORTED | #636 FR-6 / L408; #601 K3 `dmb` / L184 | OMC-DONE:67 (FR-6 absent; decision 37 has no design). No LDXR/STXR/LDAR/DMB in `omc.om`; P: `ldxr` → `error 3`. OMG:735-737 (fork children are independent; no shared-memory threads) | #636 / #601 |
| 11b | Process concurrency (fork/wait) | SUPPORTED | L408 | OMG:694-697; `seed/host/gate.sh:27-28` | — |
| 11c | GPU memory ordering / synchronization | UNSUPPORTED in Omegese (no .om GPU target) | #625 spec; #638 CPU→GPU path (row C5) | MEMBAR/BAR/atomic words exist only in the asm2 table (`seed/sm121/names.tsv`, `sm121.tbl`), checked by `seed/sm121/gate6.sh` (`evidence/SEED-0/sm121/gate6.log` "GATE6 PASS") | No ordering model for a .om GPU target |
| 11d | Races / race detection | UNKNOWN (no shared memory to race; no detector) | #625 spec | No detector found in `omc.om` or `seed/` | — |
| 12 | Serialization / format evolution | UNKNOWN for seeded formats | #621 / L832; #625 | Main formats have no unknown-version refusal: ELF output (`omc.om:5767`), OSC IR v1 (`omc.om:7079`, `gate-ir.sh`, needs C oracles), unit stream (`om/units.sh`). **Candidate:** `universal/621-source-aware-v2` @`1683caaa` `core-v2.om:2` accepts semantic header v1 and v2 | Legacy OSC IR v6 (PR #525) is C. Versioned-format refusal policy: #625 §6 `UNKNOWN_VERSION` (on branch) |
| 13a | Diagnostics | SUPPORTED (line + code) | #655 / L834, L775 | `omc.om:122-147` `omc: line N: error E`, exit E; `gate15.sh:29-47` (codes 3–23) + e12; `gate13.sh` refusal robustness | No byte span, category or incomplete/repairable status. #655 |
| 13b | Source maps / debug info (DWARF, symbols) | UNSUPPORTED (absent) | none found (row C2) | ELF has 2 program headers and no sections (`omc.om:5767-5769`) | #745 maps only formatter source; #655 maps compile-time diagnostics only |
| 14 | Language profile versioning / compatibility | UNSUPPORTED (no profile id); compatibility of old programs SUPPORTED | #625 / L835; #745 (versioned syntax); #628, #631 | No `version`/`profile` in `omc.om` except OSC IR. Identity is the binary pin (`om/units.sh:8`, `seed/out/omc.sha256`). Compatibility checks: `seed/host/regression.sh` (om4 = omc byte for byte on the original subset), `gate17.sh` (differential fuzz against `pinned.om`), `gate11.sh`, `gate18.sh`. **#625 v1** (`universal/625-semantics` @`69790fb4`, `spec/universal/semantics.md` §4) has profiles only for C17 / Rust / Python / legacy OSC v5, none for seeded omc. Independent corrective review: [receipt](https://github.com/aien-dev/omega-mv/tree/b00de4a604c001d4c304c18dbb68c49c57984df3/evidence/UNIVERSAL-625/independent-corrective-review) | Seeded-omc profile in the #625 contract (L835 `spec/omegese-om2*.md`) |
| 15a | AArch64 Linux host | SUPPORTED | L408; #650 (DDC) | `omc.om:1-4`, `:5767-5769`; OMEGA-SEED; self-identity check `seed/omega-from-seed.sh:52-58` | Independent qualification is scoped: `evidence/SEED-0/ir-trap/review.md` covers the IR-trap repair only. #650 diverse double compilation is open |
| 15b | sm_121 words | SUPPORTED for asm2 table words; UNSUPPORTED as an omc target | L117 (done); #638 | `seed/sm121/gate6.sh` (asm2 bits = C encoder); chip receipt `evidence/SEED-0/sm121/chip/receipt.json` (not rerun; no GPU in this audit). omc has host-side device ioctl words only (`omc.om:3817,4152`; `seed/host/gate.sh:46-50`) | omc emits no GPU code. #638 |
| 15c | macOS ARM64 (L615) | UNSUPPORTED on main; **candidate QUALIFIED** | L615 (done), L624 publication (open); #634 | `dev/macos-target-public-L615` @`f67fea2c6` (`ports/macos-arm64/adapter.om`, `CONTRACT.md`); review `ports/macos-arm64/evidence/final-review/independent-review-80d88a6.md`; also `verify/macos-native-L615` @`80d88a6a`, `dev/macos-arm64-L615` @`f43e6755` | Not merged. L624 / #634 |
| 15d | x86-64 (FR-9) | UNSUPPORTED | L801; #629 | OMC-DONE:70 (FR-9 absent); no x86 emitter in `omc.om`; no x86 branch in `aien-dev/omega-mv` | L801 shows coordination only (no code). #629 / L801 |
| 15e | RV64 (note) | UNSUPPORTED on main | #630 | Candidate `universal/630-qemu-preflight` @`c02989f2` (preflight) | #630 |
| 16a | Source adapters on main | UNSUPPORTED (absent) | #624 / L830 | No `src/source-aware`, `src/adapters` or `spec/universal` on main | — |
| 16b | #621 codec, #622 C import (u64) | **candidate QUALIFIED** | #621 / L832; #622 / L608 | `universal/621-source-aware` @`cd2c41f3`, `622-c-u64-import` @`f5b5b3f6`; independent review PASS (bounded), #621/#622 comment `UNIVERSAL-L602-REVIEW-DISPOSITION` 2026-10-22 | Not merged |
| 16c | G1 C17 ↔ .om (import, lower, export, native call) | **candidate QUALIFIED** | #624 / L830; #623 validator; #622 / L608 | #624 comment 2026-10-25 "G1 QUALIFIED at `universal/624-g1-integration` @`2ef48347`" (the current head `9cae82c8` contains it). Five-way comparison, 54/54 native; [hosted run 40203533129](https://github.com/aien-dev/omega-mv/actions/runs/40203533129). Lanes: `624-lower` @`fc2c2f93`, `622-c-export` @`e8227f6d`, `623-translation-validate` @`e671e7be`, `624-retarget` @`42117f51` | Not merged. Lane automation is Python/Node (`mk/*.mk` → `python3 tests/universal/translation/*.py`), not .om |
| 16d | Rust #626, Python #627, C++ #739, Zig #740, Go #741 (u64 profiles) | candidate, own selftest only | #626/#627/#739/#740/#741 via L830; C++/Zig/Go need #621 v2 (L832) | `universal/626-rust-u64` @`4b7e8b76`, `627-python-u64` @`8439284e`, `739-cpp-u64` @`0c1c1b3c`, `740-zig-u64` @`dbe268cf`, `741-go-u64` @`07b0e889`; `src/adapters/<lang>-u64-{import,export}.om`; tests `mk/<lang>-u64.mk` | No issue comments or independent receipts found on #626/#627/#739/#740/#741. Not merged |
| 17a | #669 branch displacement | SUPPORTED | #669/#673 L789 | `omc.om:1533-1545` (`disp19`; error 24; backward limit 2^64−1048576), `:1552-1554` (`patch26`, range by construction); `seed/qualification/security673/gate669.om` + README (integrated by `078455c9c` from `7a169290`) | Independent review of predecessor `e3899592` was "changes required" (#669 comment 2026-10-23T23:05Z). No post-correction independent review found. gate669 is not wired into tests/gates. Branches `fix/security673-{qualified,reviewed,supervision,branch-range,benchmark-boundary}` are unmerged |
| 17b | #670 least authority / #671 hostile parsers / #672 device isolation | UNSUPPORTED (#670, row 9c) / UNKNOWN (#671, #672 for .om) | L789 | No .om implementation on main | L789 |
| 17c | Timing / secret-dependent leakage (crypto units SHA, BLAKE3) | UNKNOWN | #748 states it (L835/L838); #673 places covert channels out of profile | No constant-time claim or timing test in `om/`, `seed/qualification/security673/` or `omc.om` | No owner for a constant-time codegen contract or test (row C3) |

## Candidate missing dependencies

Only gaps that inspection showed to be unowned. Searches were `gh issue list -R aien-dev/omega-mv --state open --search "<term> in:title,body"` plus a ledger grep. None of these is created as an issue here.

- **C1. CPU SIMD/vector semantics in .om (row 3).** There is no vector type or word in `genkw.awk:16` or `omc.om`. Searches for `SIMD` and `NEON` returned no open issue. #625 lists "vector semantics" only as specification scope, and FR-1..FR-9 (OMC-DONE:62-70) have no vector entry. #748's compact-SHA performance work may need it.
- **C2. Debug info for omc output (row 13b).** The output has no symbols, line table or DWARF (`omc.om:5767-5769`). A search for `DWARF` returned nothing. #655 and #745 cover compile-time diagnostics and formatter mappings, not runtime source maps.
- **C3. Constant-time / secret-independent timing for .om crypto (row 17c).** #748 asks only that timing-leak guarantees be *stated*. #673 asks that covert channels be declared outside the profile. `constant-time` and `timing leak` searches found no implementing or test owner.
- **C4. Seed gate wiring and the L798 owner (rows 5b, 6b, 10a, 17a).**
  - OMEGA-SEED (`seed/host/check.sh`) runs neither `gate5.sh` nor `gate6.sh`. gate5 is the rung-5 correctness checks, including recursion, the stack trap and fib under omc. gate6 is the 26 struct cases.
  - `gate669.om` is not referenced in `tests/gates/`.
  - #660 records these gates only as manual author runs. No issue was found for wiring them (`gate5`, `check.sh` searches).
  - Ledger L798 (own/free drift) is `unassigned`, and its claim that break/trap are refused is wrong at main (gate7). `own` has no gate case.
  - Natural owner: L408/L834 (seed compiler).
- **C5. Memory-ordering model for a future .om GPU target (row 11c).** #625 requires the specification. #638 (CPU→GPU path), #632 and #593 do not mention fences or ordering for Omegese, and searches for `memory ordering` and `fence` returned only runtime/perf issues (#519, #533, #672). Owned at spec level only.

## Observed drift (owned; for the owner, not new work)

- **D1.** `docs/one_layer/OM_COMPLETION.md` (last changed `53a48720a`, 2026-10-23) is out of date in two places:
  - FR-5, mmap absent (:66): `omc.om:3853` has a mapping table, tested by `seed/host/gate.sh:53-61`.
  - FR-7, C ABI absent (:68): omega-bridge native ABI v1 exists.
  Owner: L408.
- **D2.** `OMG` §3:61-69 and §9.8 agree with the source (break/continue/trap implemented). Only ledger L798's text is stale.

## Appendix A: probes (pinned omc 734f0a39, `/tmp/zl/main/seed/out/omc`, `nice -n 19`)

Inputs are newline-separated Omegese. Result is compiler stderr and exit code, plus the run exit where given.

| Probe | Source | Result |
|---|---|---|
| branch_only_write | `fn main to int / c = int 1 / if eq c 1 / x = int 5 / end / ret x / end` | `line 6: error 3` |
| same_array_twice | `fn g a edit uchar 4 b edit uchar 4 to void / end / fn main to int / q = new uchar 4 0 / g q q / ret 0 / end` | `line 5: error 19` |
| vector_word | `x = vadd int 1 int 2` in main | `error 3` |
| own_word | `own x` in main | `error 9` |
| free_array | `a = new uchar 4 0 / free a` | `error 9` |
| import_word | `import sha` as line 1 | `line 1: error 16` |
| hex_literal | `x = uint 0x10` | `error 6` |
| fm_self_recursion | `fn f n ulong to ulong / ret f n / end` (no main) | `error 9` |
| pm_self_recursion_deep | `f(n) = 1 + f(n-1)`, `f 100000000` from main | compiles; run exits 106 (trap 6) |
| atomic_word | `x = ldxr int 1` | `error 3` |
