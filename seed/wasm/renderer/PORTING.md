# Porting Allen's C scene to seeded Omega and the IR memory backend

Status, kept separate:

| Item | Status |
|---|---|
| `glow.om` and its fixture/oracle harness | IMPLEMENTED; register-heavy expressions split into bounded helpers |
| Native parity (`gate-glow.sh` sections 1-3) | EXERCISED on Linux AArch64 with pinned `befb8199…8a1d`: full buffers identical (55,628 bytes, pixel difference 0), deterministic output, three mutants differ |
| Wasm parity (`GLOW_WASM=1`, section 4) | NOT_RUN. It needs the IR memory backend. At this source, IR mode refuses `global` (OMEGESE 11, error 9). |
| The rest of the scene | PROPOSED: only the mapping below exists |
| Memory wire format and JS ABI | PENDING: the parent's spec PR. Section 6 lists what it must define. |

Nothing here is browser integration, hardware qualification or deployment.

## 1. Source pin

The C reference is aien-dev/aienux `da55660ea7337f2b6a42095fcd1fb6ddebe1cc7c`, `native/kernel/ui/`. The same bytes are
copied unchanged into aien-dev/aienos.com `om/allen-scene/native/` and bound there by `native.sha256`.
`native-pin.sha256` in this directory repeats the four digests the glow oracle compiles
(`paint.c`, `paint.h`, `allen_font.c`, `allen_font.h`). These are copies of the recorded digests, not new
measurements. `gate-glow.sh` checks them before it compiles anything. If `AIENUX_GIT` is set, it also
byte-compares the files with the aienux commit.

## 2. How the glow parity works

1. `oracle-glow.c` links the unchanged `paint.c` and `allen_font.c` and calls the original
   `ck_glow`. Only the fixture lives in that file: the case reader, the canvas reset and the gradient
   law it shares with `background.om`. The oracle is built with UBSan in fatal mode, so a case that
   reaches C undefined behaviour stops the gate instead of producing a reference.
2. `glow-cases.tsv` is the single list of cases. It is read by the C oracle, by the gate's generator
   for the native `main`, and by `compare-glow.js`. Every frame's full canvas is compared byte for
   byte as little-endian uint32 pixels, including the pixels the glow never touches.
3. The cases cover:
   - an unclipped and a clipped gradient background;
   - a glow cut by the clip rectangle on the right and bottom;
   - a glow lying entirely outside the clip;
   - centres off the canvas at negative coordinates, which exercises arithmetic `>>`;
   - channel saturation in `ck_add`;
   - `a = 4294967295`, which exercises uint32 wrap in `w * a` and `ck_ch(c) * a`;
   - a sub-pixel radius;
   - the no-op guards `r = 0`, `r < 0` and `a = 0`;
   - a 37x23 canvas, so a stride that confuses w and h is caught.

   Colours are copied from the pinned callers.
4. Determinism: the oracle runs twice, omc compiles twice, irwasm translates twice, and each pair is
   compared.
5. Mutants: the gate rewrites one exact line of `glow.om` for each of three mutants and requires the
   result to run and to differ from the oracle. The mutants are the falloff shift 32 to 33, the
   saturation 255 to 254, and dropping the right clip. With `GLOW_WASM=1`, `compare-glow.js` must
   also exit 1 with `pixel mismatch` for each mutant.

`compare-glow.js` reads pixels through the scalar export `fbget`, so it does not assume where the
backend places `fb`. Once the ABI is agreed, a direct linear-memory comparison can be added beside it.

Commands (nice, ionice idle, at most -j2; the full command requires backend integration):

```sh
ALLEN_NATIVE_DIR=/path/to/pinned/native nice -n 19 ionice -c3 sh seed/wasm/renderer/gate-glow.sh            # native; exits 3 (wasm NOT_RUN)
GLOW_WASM=1 ALLEN_NATIVE_DIR=/path/to/pinned/native nice -n 19 ionice -c3 sh seed/wasm/renderer/gate-glow.sh # full
```

## 3. C-to-Omega rules used by `glow.om`

Every rule below holds in the existing language at this source (`seed/out/omc` `befb8199…8a1d`).

| C | Omega |
|---|---|
| `int32_t`, `uint32_t`, `int64_t`, `uint64_t` | `int`, `uint`, `long`, `ulong`. There are no implicit conversions, so each one is written out (`long x`). |
| `ck_canvas *cv` (pointer to pixels) | `global fb uint 65536`. A global is named directly and is never passed (error 19). Canvas `w` and `h` live in the cells `cf 0` and `cf 1`. |
| `const ck_rect *clip` | The cells `cf 2..5`, set by `fbclip`. IR mode refuses structs, so there are no struct globals. |
| more than 6 parameters (`ck_glow` has 7) | Canvas and clip become state that is set earlier. The export takes five scalars. |
| per-call constants (`r2`, `inv`, the fitted span) | The cells `gl 0..5`. These keep each helper under the 16-register pool (error 12) and the 6-parameter limit. |
| `continue` | An `if` around the rest of the body. IR mode refuses `continue` (error 3). |
| `for (...)` | `while ... bound N`. Each N comes from the canvas bound w, h <= 65536 or from the isqrt count of 32, and is written in a `note`. |
| early `return` | An enclosing `if`, because a typed function's `end` must not be reachable (error 18). |
| `>>` on signed values | `shr` on `int` or `long`, which keeps the sign (floor), as GCC and Clang do. |
| uint32 multiply wrap | `mul` on `uint`, which wraps mod 2^32. |
| signed overflow (undefined in C) | Trap 1. Fixtures must stay in the defined range, and UBSan proves the oracle did. |
| `(uint32_t)` and `(int32_t)` casts | Checked conversions (trap 5). Every cast in `ck_glow` provably fits: `f` < 2^9, `isqrt(r2 - dy2)` <= r. |
| helper order | Callee before caller, because IR mode calls only earlier functions. |
| `CK_RGB` | `ckrgb`, so every colour stays the literal triple copied from C. |

Names: locals and globals have at most 8 characters, and none of them may be a keyword or a host word
(checked against `seed/omc/genkw.awk`).

## 4. Mapping the rest of the scene (PROPOSED)

These units total 8,478 lines (`wc -l`), counting the pinned native files plus `adapter.c`. Each item
lists what the existing language already covers and what it does not. Gaps are stated as gaps, not
solved here.

- **`paint.h` inlines** (`ck_scale`, `ck_mix`, `ck_smooth`, `ck_ease_out`, `ck_prog`, `ck_cover`, `ck_clip`
  family): scalar functions only. These can go to IR v1 today, and each needs an exact-result table row
  against the C.
- **`paint.c` tables** (`sin_t[4097]`, `exp_t[4096]`, `log_t[257]`, lazily filled by `setup()`): globals
  plus an explicit `setup` export with a `ready` cell. The generation loops port as written. The
  `step[14]` atan2 constants and the `ck_hash` multipliers are C hex and decimal literals: copy them as
  decimals, and convert any hex value with Wolfram, quoting the query beside the code.
  `__builtin_clzll` in `ck_log2_q16` becomes a bounded loop of at most 64 passes.
- **`ck_thread`, `plot`, `ck_sd_rrect`**: the same pattern as glow. Clip and canvas come from the state
  cells, and the per-call state goes in a cell global.
- **`allen_font.c`**: the glyph strings are `text` in program mode, but IR mode refuses `text`
  (OMEGESE 11). The glyph cache is a `pool_used` arena, which needs a fixed global pool (there is no
  heap; decision 24). How read-only data enters the module is a memory-spec decision (section 6).
- **`mind.c`** (120 points, 300 edges, 64 sparks): fixed-size arrays become globals, and its draw is a
  sequence of `ck_glow` and `ck_thread` calls. `static const` arrays (`spins`, `squeezes`, `hearts`) have
  the same data-entry question as the font.
- **`night.c`**: the caller supplies memory (`ck_night_mem_bytes`, `void *mem`, `ck_night_picture_take`).
  That has to become fixed globals sized for the bounded browser resolution, because Omega has no
  address word and no heap. The `bayer[16]`, `band_t` and `pow2_t` tables are handled like the
  `paint.c` tables.
- **`flow.c`**: `ck_flow_mark(ck_flow *, void (*mark)(ck_rect))` takes a function pointer, and Omega has
  no function values. The proposal is to write the dirty rectangles into a fixed global list that the
  caller reads. That changes the interface, so it needs its own parity check.
- **`sky_real.c` and `sky_data.c`** (5,610 lines of catalogue data): bulk read-only data. Its attribution
  terms in `provenance.txt` travel with it. How the data is placed in memory waits on section 6.
- **`chirp.c`** (audio): table setup plus `int16_t` output fills an output buffer global.
- **`adapter.c`**: the web entry and lifecycle. Its scalar exports (`scene_init`, `scene_step`,
  `scene_draw` and so on) map directly to scalar Omega exports. `scene_pixels()` returns an address,
  which has no raw-address source spelling; the JS side obtains the region offset through the ABI in section 6.

Capacity facts at this source: the IR wire allows at most 32 functions per unit and
4,096 instructions per function, but the current omc IR producer has a smaller
per-function instruction table; the native register allocator has 16 registers.
Do not infer that a wire-format limit is the producer's implemented capacity.
The full scene needs explicit capacity qualification; globals shared across
separately translated units are not part of this memory profile.

## 5. Recipe for each further primitive

1. Pin the C function and each helper it reaches. Add their digests to `native-pin.sha256`.
2. Port it by the rules in section 3, keeping helpers before their callers.
3. Extend the case list. Make the oracle call the unchanged C, compare full buffers, run each artifact
   twice, and add at least one one-line mutant that must differ.
4. Run native parity first, then wasm parity, and report each result by its status word.

## 6. Memory ABI: for the parent to fill once the spec PR merges

This section is not written yet on purpose. It must state the agreed values, citing the merged spec:

- how an IR v2 unit declares `global NAME T N`, and the wire encoding of loads and stores;
- the wasm memory: whether it is exported and under what name, the initial and maximum pages, and the
  placement and alignment of each global;
- how JS locates `fb` (by an export, a fixed offset or a descriptor), and the byte order (wasm memory is
  little-endian);
- zero initialization, and how read-only data (font, tables, sky catalogue) enters memory;
- whether globals can be shared between units, and the limit on total memory size.

Until then, the wasm harness relies only on the scalar exports `fbsize`, `fbclip`, `fbgrad`, `ckglow`
and `fbget`.
