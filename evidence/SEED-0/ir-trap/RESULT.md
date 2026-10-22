# Seeded Omega IR trap repair (L524)

## Scope and ancestry

This is a bounded repair to the **experimental** R6 scalar IR emitter, not completion of R6 and not general IR qualification. The source milestone is `6849cae160ef890a147eb829ae39c3c68f733eda` on `fix/seed-ir-trap`, based on the structs milestone `57f5f4258cebf3d5b2c22853cd062e351265c525`.

The starting IR implementation was inherited uncommitted work in the L408 worktree. Its source SHA-256 is `04fc0d5f6dcfba93bcd9adecaf63bbc7c883adb05d7e48a7401af3fb9d9b4ecd`; `inherited-ir.patch` preserves that input separately. `repair.patch` shows this repair alone. The original compiler source remains unchanged there; this repair has its own worktree and branch.

Production compiler source: `seed/omc/omc.om`, SHA-256 `6ce0e53fbf7a99454cfbfe1e6790c6c0065f4e1d8538dd95fad9ad9b8b571f51`.
Compiler ELF: SHA-256 `d61d76156688320aa7f70327a34cc34901814bfbaa15e4659bc9cf13714fbf76`.

The rebuilt ancestry is seed0 handwritten hex -> hex1 -> asm2 -> om4 -> Omega-written omc -> two self-builds. The outside initial conversion is the disclosed POSIX-shell `load0.sh`; shell/file/hash utilities and Linux remain host dependencies. No C compiler builds omc in that reconstruction. Fixed-point identity is self-consistency, not independent proof of compiler correctness.

## Defect and repair

`kw` maps `trap`, `break`, and `own` to the same unsupported-word code, 77. The experimental IR dispatcher let that code fall into assignment parsing. Thus `if gt a 10; trap 3; end; ret a` emitted accepted IR which returned `a` for 11 instead of trapping.

The repair implements `ir_trap` in Omega:
- Require an untyped trap-code literal from 1 through 14, then end of line/input.
- Emit `TRAP code`, followed by an unreachable zero-valued `RET` of the function's return type (or a void return), as OMEGESE section 4 requires.
- Refuse later statements on the terminated path; retain structural `end`/`else` processing.
- Explicitly refuse the still-unsupported `break` and `own` statements instead of silently discarding them.
- Encode the `void` keyword's parser ID 76 as the IR void type tag 0.

## Verified checks

- Red baseline: **67 checks, 50 failures**, including actual wrong returned values, not only a parsing error (`red.log`).
- Green: **67 checks, 0 failures**; **19 valid programs**, **11 refusal cases**, **105 executed interpreter/native comparisons** (`green-final.log`). All trap codes 1..14, unsigned boundary values, signed/bool/void returns and nested branches are included.
- **3 compiled mutants caught**: wrong trap code, wrong void type tag, omitted code-range check (`mutants.log`). Each mutant must first compile successfully to a nonempty AArch64 ELF and pass its compiler fixpoint; build failures cannot count as detection.
- Full seed reconstruction and names-pass probes: `birth.log`.
- Inherited scalar checkpoint: **10/10 byte-identical IR programs**, checker2 accepts each, changed arithmetic differs (`scalar-checkpoint.log`).
- Inherited structs: **26 cases**, including field-swap control (`structs.log`).
- All inherited Gate5 **correctness sections 1..6b**: **177 cases**, including all original negative controls and **4,860** cut/altered inputs (`gate5-correctness.log`). The original gate script is unchanged.
- Legacy `make test-omegese`: see `legacy-omegese.log` and the final qualification disposition below. Its default optional tokenizer check is supplemented against actual tokenizer files in `tokenizer.json`.

### Wolfram verification

`wolfram.wl` and `wolfram-result.txt` contain actual Wolfram Language evaluator input/output. They independently check:
- Little-endian keyword packing: `fn = 28262`, `trap = 1885434484`.
- Exact expected outputs for inputs 0, 1, 10, 11, 255 and 4294967295.
- Actual emitted IR lengths, SHA-256 hashes, header fields, block layout, trap/return instructions and zero contract-count footer for both scalar and void cases. The byte lengths and full hashes agree with the files on the Spark.
- Compiler word count `(116560 - 176)/4 = 29096` and the executed-fixture accounting.

C-built Omegese/omega_emit/checker2, the OSC interpreter/native backend and Rust-built tokcount are explicitly **test oracles**, not build/runtime dependencies of the reconstructed seed-born compiler. In particular, IR behavior is exercised through the reference native backend; this does not claim a completed seed-native IR loader/backend.

## Reproduction

On AArch64 Linux, in a full checkout:

```sh
make omegese omega-emit checker2       # legacy test oracles only
sh seed/omc/qualify-ir-trap.sh
```

The harness rejects failed compiler exits and empty/non-AArch64 ELF artifacts before execution. No GPU model, service, driver, module or boot setting is changed.

## Limits and outstanding acceptance

- **Full Gate5 performance section 7: NOT_RUN.** Its exclusive quiet-lock admission was blocked by a long-lived fixture server (pid 986427) holding an inherited shared lock fd, plus an existing suite. Only this repair's never-started waiter was cancelled; no holder was killed. All Gate5 correctness assertions ran separately unchanged. No performance claim is made.
- The inherited IR emitter still has known unsupported/incorrect cases outside this repair, including parenthesized values, nonliteral `for` counts, and multiple functions. This branch must not be presented as general IR-ready or merged as completion of R6.
- Native ELF compilation, IR emission, reference backend execution and actual GPU execution are separate capabilities. No new GPU execution or C/Rust production retirement is claimed.
- Independent read-only review is tracked as **L527** on exact source milestone `6849cae1`; no independent-review completion claim until its findings return.

## Final qualification disposition

Pending final suite completion; see `qualification.log` for the job's own exit code.
