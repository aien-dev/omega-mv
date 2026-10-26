# om/sha256: SHA-256 in Omega (OM-2), linked into the product

The SHA-256 API of `src/sha256.h` (`sha256_ctx`, `sha256_init/update/final/hash`) is the OM-2 unit
`sha256-om2.om`, compiled by the seed-born `omc` and called in-process through the omega-bridge
native call ABI v2 (`docs/NATIVE-ABI.md`, aien-dev/omega-bridge feat/slice-abi a8959fb1). No C computes or
adapts it: `src/sha256.c` and the OM-1 unit with its 4096-byte adapter are gone (#747). The 894
call sites are unchanged (`om2-evidence/AUDIT-L838.txt`).

| file | role |
|---|---|
| `om/sha256/sha256-om2.om` | the implementation: `export fn sha256_init/update/final/hash`; `layout shactx 104`, `fresh` schedule scratch, `for ... unroll` rounds and schedule, `rotr`, hex K/IV from the L836 fixtures, unsized `view uchar *` input, `out uchar 32` digest |
| `src/omega_om_units.c` | generated: the unit's machine code byte for byte as omc wrote it, its ABI v2 binding (`omega2_sha256_*`, `omega2_ident_sha256_om2`), the BLAKE3 unit's v1 binding, and the C API `sha256_init/update/final/hash`, each calling `omega2_sha256_*` and `abort()` on a nonzero status |
| `src/omega_native.h`, `src/omega_api_decl.h` | generated: declarations only; `sha256_ctx` from the layout (offsets static-asserted), `SHA256_*` defines, the `sha256_*` prototypes. Including them references no C library function, so a freestanding caller (rx_cortex) still needs only `sha256_*` |
| `src/omega_om_units.ident` | generated: each unit's source sha256, compiler pin, ABI and profile |
| `src/sha256.h` | includes `omega_api_decl.h`; no logic |
| `om/units.sh` | regenerates the four generated files; `--check` proves they are what the pinned tools produce (a stale unit, compiler or profile fails) |
| `om/sha256/parity/` | parity against the replaced C retrieved from git history (`run.sh`), section 7 receipts (`receipts.c`), throughput probes (`bench.c`, `small.c`) |
| `om/sha256/om2-evidence/` | caller audit, compile log per compiler slice, baseline at acb3f15c, cutover plan, desk check, S4 run |

Pins: omc `49923639…803ec` (om2/compiler-s4 f6a6492fd, born by `seed/omega-from-seed.sh`);
generator `src/native.om` of omega-bridge `a8959fb1` (om/omega-bridge.lock; sha256 `24f252b9…1fac`).

## Behaviour (spec/omegese-om2.md section 7, errata E3)

- Updates of any length, including 0 (any pointer) and at least 2^32 bytes; any split points.
- `sha256_hash` may write its digest over its input: the digest is copied out after the call.
- A byte copy of a live context is an equivalent context.
- After `sha256_final` the context's contents are unspecified (this unit leaves it unchanged);
  `sha256_init` makes it fresh again. No whole-context equality with the old C is claimed.
- A nonzero bridge status (a NULL pointer with a nonzero length, a context overlapping the data,
  a trap) aborts the process, as the old adapter did.
- Rolled loops trap after 1,000,000 passes, so whole blocks are hashed in runs of 2^25 bytes.

## Evidence

Qualified by L836 (om2/qualification 478bdecd, candidates/sha/RESULTS.txt): 158 vectors, E3/section 7
receipts, 17/17 unit mutants killed, dynamic traces 28/28 identical. The static leakage scan of
update/final is NOT_RUN; no constant-time claim is made. Throughput against the replaced C is in the
cutover PR, measured with `parity/bench.c` and `parity/small.c`.

## Building the generated binding

`src/omega_om_units.c` embeds each unit's machine code exactly as the pinned compiler wrote it, and its
runtime entry table is declared with GNU range designators, which ISO C forbids. The four suites that
compile it with `-pedantic` and `-Werror` (ANS, estimation, Brownian active and the physics baseline;
`mk/estimation.mk`, `mk/ans.mk`, `mk/brownian_active.mk`, `mk/physics0.mk`) therefore append `-Wno-pedantic`
to their flags: only that one diagnostic is waived, and the handwritten sources in the same commands are
pedantic-clean today, so the rest of the strict set still applies to every file they compile.
The generator fix is omega-bridge #17 on the ABI-v2 line; when it lands, regenerate the binding
with it and drop those flags.
