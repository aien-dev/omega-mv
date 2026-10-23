# Issue 659 evidence

Evidence class: **EMULATED**. The host is x86_64 Linux. QEMU executes both the
AArch64 seeded compiler and the AArch64 test programs. No performance timing,
Spark/GB10 execution, production dispatch or power-loss qualification is claimed.

Functional source is recorded separately from evidence-only follow-up commits.
The final source-bound run is in `qualified/receipt.json` when present. Files
directly in this directory are chronological development evidence and do not
independently bind a clean source commit. They are preserved, including the
initial failing controls and the later delegation rights-profile correction.

## Tools

- Seed/compiler source: `aien-dev/omega-mv@57f5f4258cebf3d5b2c22853cd062e351265c525`.
- Compiler SHA-256: `01b1545b23e8ff40cb5691e20b5df910ea4acaefde3495f0d62d270465b98cd6`.
- QEMU: `qemu-aarch64 version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)`.
- QEMU executable SHA-256: `e4f8d99e9ff69c3cefffab71cee358ce2af1ecba1282d04c3eeb44ef76f5a71e`.
- Ubuntu package SHA-256: `5bb397f66063efa349f6fd5cb3b68cd96f29edd0994e4ba5115cf0859a716bf0`.
- The package was extracted into the scratch tool directory. It is an external
  execution emulator, not a project build/runtime dependency.

The seed reconstruction starts with the existing `seed/rung0/load0.sh`, then
runs seed0 -> hex1 -> asm2 -> om4 -> omc stage1 -> stage2 -> stage3 under QEMU.
Each reconstructed rung matches the pinned published binary. All three omc
stages match one another and `seed/out/omc`.
`seed-rebuild.sha256` records the actual output hashes. This checks the
reconstruction and fixed point; it is not diverse-double-compilation proof.

## Replay commands

The source and tools must be pinned before replay. `qemu-aarch64-static` below
is the executable with the hash above, and `OMC` is the verified compiler.
Use a private output directory; never replace the source compiler or evidence.

```sh
OMC=/absolute/path/to/verified/omc
QEMU=/absolute/path/to/qemu-aarch64-static
OUT=/absolute/path/to/private-output
mkdir -p "$OUT"
cat native/control/src/action.om native/control/tests/action.om > "$OUT/action.om"
"$QEMU" "$OMC" < "$OUT/action.om" > "$OUT/action.elf"
chmod +x "$OUT/action.elf"
"$QEMU" "$OUT/action.elf"

cat native/control/src/delegate.om native/control/tests/delegate.om > "$OUT/delegate.om"
"$QEMU" "$OMC" < "$OUT/delegate.om" > "$OUT/delegate.elf"
chmod +x "$OUT/delegate.elf"
"$QEMU" "$OUT/delegate.elf"

cat native/control/src/reuse.om native/control/tests/reuse.om > "$OUT/reuse.om"
"$QEMU" "$OMC" < "$OUT/reuse.om" > "$OUT/reuse.elf"
chmod +x "$OUT/reuse.elf"
"$QEMU" "$OUT/reuse.elf"
```

Expected final output, respectively:

```text
action checks=219 000
delegate checks=074 000
reuse checks=074 000
```

Each program returns 0 on those results, 1 on a failed assertion and 2 on failed
report output. Each assertion is executed in Om. The reporting counters are not
host-calculated. Source concatenation and process invocation are external
build/test transport, not a shipped foreign test implementation.

For the three baseline negative controls, replace `src/MODULE.om` in the same
commands with `tests/negative/MODULE-stub.om`. Each must compile with exit 0,
run with exit 1 and print failed assertions. The original RED tests were run
before writing each real implementation; the final-source replay repeats the
controls against the current tests.

For the ten guard mutants, apply the corresponding `qualified/*.diff` with
`git apply --unidiff-zero` in an isolated scratch checkout to a
scratch copy of the production module, then compile it with the unchanged Om
tests. Every mutant must compile successfully and fail assertions with exit 1.
A compiler error, signal or timeout does not count as detecting a mutant.

The three compile-refusal inputs are concatenated after the named production
module and compiled in the same way:

| Input under `tests/negative/` | Module | Expected compiler exit |
|---|---|---|
| `wrong-action-size.om` | action | 7 |
| `aliased-action.om` | action | 19 |
| `wrong-payload-size.om` | reuse | 7 |

No test binary should be run from a refused compilation. Aliasing a mutable
record as its own independent authority-facts argument must remain refused.

## What remains

Final functional source: `baa1587fc1542177595b90dccf36f98b1e294b0a`.
The clean-source replay in [qualified/receipt.json](qualified/receipt.json)
records 367 passing assertions, three detected always-accept stubs, ten
detected compiled guard mutants and three expected compiler refusals. Each
mutant ran and failed assertions; none was counted from a compiler error.

- Independent review of the final functional source: NOT_RUN.
- Trusted authority/receipt verification and durable store/dispatch integration:
  NOT_RUN. These modules accept facts produced by those owners.
- Concurrent actors, actual crash/restart, persistence failure and revocation
  racing with dispatch: NOT_RUN at the integration boundary.
- Native AArch64/Spark/GB10 and other hardware profiles: NOT_RUN.
- Full existing runtime suite: NOT_RUN; no legacy runtime source or build target
  changes. The full new component suites and compiled controls are exercised.
- Release/merge/publication: retain the existing Renda/desk workflow. A passing
  component receipt is not permission to mark the parent issues complete.
