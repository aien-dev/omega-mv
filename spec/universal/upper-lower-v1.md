# Upper S1 lowering v1 (#624 G1, compiler/core lane)

Implements `translation-v1.md` section 5 (contract sha256 `063f1d63…de86e`, branch
`universal/624-translation-g0` @ `82cd7692f`). It adds no format and changes no existing one.

## Tool

`tools/adapters/lower [NAME] < envelope > unit.om`. The wrapper is transparent host dispatch: it
checks `build/L831/build.json` (compiler sha, the sha of each closure source, the binary sha) and
refuses 78 `LOWER_STALE_OR_MISSING_BUILD` when anything has drifted. It then execs the native
binary with the same arguments. All validation, hashing and text generation run in seeded Omega.

Build (`make -f mk/lower.mk lower`, `tools/adapters/build-lower.py`):
- The compiler is the section-1 native realization compiler, omc sha256 `734f0a39…6b61`, from
  `OMEGA_NATIVE_OMC`, by default `~/workspace/omega-seedqual-20261025/seed/out/omc`.
- It compiles the concatenation of three unchanged or owned sources:
  - `src/source-aware/vendor/sha256.om` (hash-checked by `tools/source-aware/imports.sha256`);
  - the #621 core `src/source-aware/core.om`;
  - `src/adapters/upper-lower.om`.
- The core is the #621 owner's file. It is prepended unchanged, never edited.
- This compiler is used, not the importer's `191b776e`, because only `734f0a39` has argv
  (`argcnt`, `argread`), which `lower NAME` needs.

## Behaviour

1. The tool reads stdin to EOF, up to 8192 bytes. A 8193rd byte means 65.
2. The core's `decode` validates the whole envelope as source-aware v1, sidecar included. It is
   linked in-process, one envelope per process; its fixed globals are not reentrant, so harnesses
   must run separate processes, one at a time.
3. The unit must also be S1: language 1, profile 1, runtime 0, ABI 1, and every op of type 1 with
   effects 0 and alias 0, with exactly these fields:

   | op | meaning | attribute | obligations |
   |---|---|---|---|
   | 9 | input | 0 | 1 |
   | 1 | constant | 1 | 0 |
   | 2, 3, 4 | add, sub, mul | 1 | 1 |

   Input slots must form the permutation 0..n-1. The core guarantees uniqueness and slot ≤ 5, and
   this tool checks slot < n. The core already checks operand index < own index.
4. The output is the section-5 unit, byte-deterministic, one LF after every line including `end`.
   - The note's hex is the unit's semantic identity, `SHA256("omega.source.semantic.v1" ||
     semantic bytes)`, the same digest as `sa identity`.
   - Constants are printed in unsigned decimal, from 0 to 18446744073709551615.
5. Output is fully buffered and written only after acceptance. A refusal writes nothing to stdout
   and one named line to stderr.

| status | stderr | cause |
|---|---|---|
| 64 | `LOWER_BAD_COMMAND` | more than one argument, or a bad NAME |
| 65 | `LOWER_MALFORMED` | input over 8192 bytes, or any core decode failure other than 68 (65/66/67/69..78) |
| 68 | `LOWER_PROFILE_MISMATCH` | core decode 68, or a valid v1 unit that is not S1 (for example op 5, profile 2, a slot gap) |
| 77 | `LOWER_IO` | an OS read or write failure, not a semantic status |

When NAME is absent the tool uses `calc`. NAME must match `[A-Za-z_][A-Za-z0-9_]{0,62}`, and it
must not be "reserved". For this grammar, reserved means the 150 identifier keywords of omc
`734f0a39` (for example `get`, `u64`, `argcnt`) plus `v`, `i0`..`i5` and `main`. The reason, measured
with omc 734f:
- `v` clashes with the array local (error 7).
- `i0`..`i5` clash with the parameters (error 10).
- `main` switches the unit to program mode (error 9).

The coordinator approved this meaning of "reserved" on 2026-10-25.

## Native realization

1. The pinned omc compiles the unit in function mode.
2. omega-bridge `src/native.om` at `012860db` (source sha256 `2c2c53f6…3940`) binds it as
   `uint32_t omega_<NAME>(uint64_t *result, uint64_t i0, ...)`.

A 64-result unit (`new ulong 64`) with six inputs compiles and runs. No register or size limit was
hit.

## Self-test

`make -f mk/lower.mk test-lower` builds `lower` and the unchanged importer (`SA_SEED_REPO`, default
`~/workspace/omega-bridge`), then runs `tests/universal/translation/lower_selftest.py`
(`OMEGA_BRIDGE_REPO`, default `~/workspace/omega-bridge`). It covers:
- every F1 positive;
- the four accepted `c_import_controls` boundaries, including the 61-term chain at 64 operations;
- crafted semantic-only units: permuted input records, lone constants 0 and 2^64-1, a 64-result
  six-input unit, and a 63-byte name.

What it checks:
- The unit bytes must match a host rebuild of the section-5 form.
- The semantic-only envelope must lower identically.
- The native results must equal a Python bigint evaluation of the records, on boundary and seeded
  random vectors.
- Each refusal must give its status, an empty stdout and the named stderr line.

Evidence class: TESTED by this lane, DIFFERENTIAL + REFERENCE, NATIVE. It is not qualification.
