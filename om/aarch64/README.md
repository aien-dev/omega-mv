# om/aarch64: the legacy AArch64 writer, decoder and validator in seeded Omega

`aarch64.om` ports two C files of the product to seeded Omega (aien-dev/omega-mv#726,
aien-dev/aien-architecture-mv#300/#316):

| C | what it does | Omega functions |
|---|---|---|
| `src/aarch64_encoder.c` (+ `aarch64_encoder.h`) | the 25 `aarch64_emit_*` writers: range checks, refusal, word formula, little-endian store | `a64addreg` … `a64subsreg` (one per emitter), `a64put` |
| `src/aarch64_decoder.c` (+ `aarch64_decoder.h`) | `aarch64_decode_instruction`, `aarch64_validate_code_buffer` | `a64decode`, `a64validate` |
| `src/aarch64_target.h` | `Aarch64Reg`, `Aarch64Cond`, `AARCH64_MAX_CODE_BYTES` | the same values (tables below) |

The C is unchanged. This change adds the port and its parity evidence. Removing the C objects goes
through the omega-bridge native call ABI (omega-bridge #8) and is not part of it.

## Where the task text and the files differ

- There is no `aarch64_core.h`. The headers are `aarch64_encoder.h`, `aarch64_decoder.h` and
  `aarch64_target.h`.
- The decoder file also holds `aarch64_validate_code_buffer`, which callers use (`omega_verify.c`,
  `visor_realization.c`); it is ported too, with its `snprintf` messages byte for byte.
- The encoder has no error codes for bad fields. An out-of-range field makes the C print
  `A64_LEGACY_FIELD_OUT_OF_RANGE <emitter> <field>=<value>` on stderr and `abort()`. The only error
  result is -1 for a full buffer. The port returns a refusal code instead (see ABI), and the parity
  harness prints the C's exact stderr line from it.
- "Round trip encode→decode→encode identical" holds for 14 of the 25 emitters only. The C decoder
  drops fields for the other 11: ADR returns only `rd`; LDR/STR return `sf rd rn` but no offset and do
  not tell the unsigned-offset form from the post-index form; LDRB/STRB return `rd rn` only; SUBS
  returns `sf rd rn rm` and does not tell the immediate form from the register form. For those 11 the
  harness checks instead that the decode gives the expected op and every field it carries equals the
  emitter input (mark `~`).

## Enum and numbering tables (value-compatible with the C headers)

`DecodedOp` (`out 0` of `a64decode`): 0 INVALID, 1 ADD, 2 SUB, 3 MUL, 4 AND, 5 ORR, 6 EOR, 7 MOVZ,
8 RET, 9 B, 10 B_COND, 11 CBZ, 12 CBNZ, 13 MOVK, 14 ADR, 15 LDR, 16 STR, 17 LDRB, 18 STRB, 19 SUBS.

`Aarch64Cond`: 0 EQ, 1 NE, 2 CS, 3 CC, 4 MI, 5 PL, 6 VS, 7 VC, 8 HI, 9 LS, 10 GE, 11 LT, 12 GT,
13 LE, 14 AL; the emitter also accepts 15 (NV), as the C does (`CHECK_COND` is 0..15).

`Aarch64Reg`: `REG_X0`..`REG_X30` = 0..30, `REG_XZR` = `REG_SP` = 31. `AARCH64_MAX_CODE_BYTES` = 4096.

`DecodedInsn` as `out edit ulong 9`, in struct order: 0 `op`, 1 `sf`, 2 `rd`, 3 `rn`, 4 `rm`,
5 `imm16`, 6 `hw`, 7 `branch_imm`, 8 `cond`.

Emitter form numbers (corpus and harness only; the order of `aarch64_encoder.h`): 1 add_reg, 2 sub_reg,
3 mul_reg, 4 and_reg, 5 orr_reg, 6 eor_reg, 7 mov_reg, 8 movz, 9 ret, 10 b, 11 b_cond, 12 cbz, 13 cbnz,
14 movk, 15 adr, 16 ldr_uoff, 17 str_uoff, 18 ldrb_uoff, 19 strb_uoff, 20 ldr_post, 21 str_post,
22 ldr_x_post, 23 str_x_post, 24 subs_imm, 25 subs_reg.

## ABI (omega-bridge native call ABI v1)

Every exported function has at most 6 parameters, each `ulong`/`long` or a fixed-length `view`/`edit`
array of `ulong`/`uchar`, and returns `long`.

| C | Omega | result |
|---|---|---|
| `aarch64_emit_<form>(buf, &pos, max_len, fields…)` | `a64<form> fields…` (fields in C order, after `max_len`) | the 32-bit word (0..2^32-1), or refusal code `-(k+1)` |
| `emit_u32_le` (static) | `a64put b(edit uchar 4096) st(edit ulong 2: pos, max_len) w` | `w` itself when `w < 0` (nothing written), else -1 when `pos + 4 > max_len`, else 0 after the store and `pos += 4` |
| `aarch64_decode_instruction(insn, &d)` | `a64decode insn out(edit ulong 9)` | 0, or -1 with `out` all 0 |
| `aarch64_validate_code_buffer(code, len, msg, ml)` | `a64validate code(view uchar 4096) len msg(edit uchar 256) ml` | 0, or -1 with the C's message in `msg` (snprintf rules) |

So `aarch64_emit_<form>(buf, &pos, max_len, …)` is `a64put b st (a64<form> …)`. A refusal code names
the first field that fails, in the C's check order: `k` is the field's 1-based position among the
emitter's fields (for `add_reg`: sf 1, rd 2, rn 3, rm 4, so a bad `rn` gives -4). An adapter that keeps
the C behaviour prints `A64_LEGACY_FIELD_OUT_OF_RANGE aarch64_emit_<form> <field>=<value>` and aborts
on it; -1 stays "buffer full". Field names: the C parameter names (`rd rn rm rt uoff simm9 imm12 imm16
shift imm19 imm21 imm26 cond`).

`parity/unit.py aarch64.om NAME` cuts the function-mode unit for `NAME`. All 28 units compile with the
pinned omc in function mode; the pinned generator (`src/native.om` of omega-bridge `012860db`, sha256
`2c2c53f6…3940`) accepts them and the C it writes compiles with `-Wall -Wextra -Werror`, exporting
`omega_a64addreg` … `omega_a64validate`. In a separate check (a /tmp driver, not in the tree) every
corpus stream was run through those generated bindings; the output was byte-identical to the C oracle's.

## What the C does, and the port keeps

- Each emitter checks its fields before it encodes (`CHECK_REG` 0..31, `CHECK_SIGNED`, `CHECK_UMAX`,
  `CHECK_COND` 0..15, `CHECK_HW_SHIFT` shift a multiple of 16 up to 48 (sf) or 16, `CHECK_SCALED` uoff
  a multiple of 8 (sf) or 4 with uoff/size ≤ 4095). The check comes before the buffer test, so a bad
  field is refused even when the buffer is full. `mov_reg` checks `rd` and `rm` under its own name, then
  emits `ORR rd, XZR, rm`.
- The decoder tests its patterns in a fixed order and stops at the first match. Its MUL test
  (`0x7FE07C00`) ignores bit 15, so `MNEG` (MSUB with XZR) decodes as MUL; the logical/arithmetic tests
  ignore the shift type and amount; `branch_imm` is the raw unsigned field, not sign-extended; RET sets
  `rn = 30`.
- The validator: an empty buffer, a length that is not a multiple of 4 (`%zu`), the first word that
  does not decode (`0x%08x`, offset `0x%zx`), or a last word that is not RET (`op %d`), each with the C's
  exact text, truncated the way `snprintf` truncates (at most `ml - 1` bytes and a NUL; nothing for
  `ml = 0`).

## Parity

Reproduce from a fresh clone (no C oracle needed; the oracle's output is recorded as sha256 in
`parity/expected.txt`):

```
OMEGA_OMC=/path/to/seed/out/omc [OMEGA_BRIDGE=/path/to/omega-bridge] sh om/aarch64/parity/run.sh
```

It prints `AARCH64-PARITY PASS` in about 20 s on the Spark. `parity/corpus.py` regenerates the corpus
deterministically (splitmix64, fixed seeds, 3,152,381 records, 55 MB, never committed).

Oracle: `src/aarch64_encoder.c` (sha256 `b892312e…1a17`) and `src/aarch64_decoder.c` (`d9e4bf06…beac`)
at omega-mv `acb3f15c`, compiled with `gcc -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2 -Isrc`
(the Makefile CFLAGS, gcc 13.3.0, aarch64). `.text` sha256: encoder `13296478…1a03`, decoder
`2d14fdbc…c52d`, the same bytes as the `make`-built objects of other worktrees with the same sources.
A /tmp driver (not committed) feeds each stream to these objects. It catches each refusal's `abort()`
by `SIGABRT` → `siglongjmp`, with stderr on a memfd, and the record is the captured stderr line.

| stream | inputs | words | refusals | result |
|---|---|---|---|---|
| `add_reg` `sub_reg` `mul_reg` `and_reg` `orr_reg` `eor_reg` `subs_reg` (each) | 76,408: **all** sf × rd × rn × rm (65,536), every register field 32..255, 200 random, 10,000 in range | 75,536-75,538 | 870-872 | identical |
| `mov_reg` | 12,696: all sf × rd × rm, rd/rm 32..255, 200 random, 10,000 in range | 12,051 | 645 | identical |
| `movz` `movk` (each) | 216,608: **all** sf × rd(0..255) × shift(0..255), **all** imm16, 10,000 random over the types, 10,000 in range | 75,747 / 75,744 | 140,861 / 140,864 | identical |
| `ret` | 1 | 1 | 0 | identical |
| `b` | 54,585: edges, every imm26 within 4096 of -2^25, 0, 2^25-1, 10,000 random in range, 10,000 over int32, 10,000 in range | 36,559 | 18,026 | identical |
| `b_cond` | 47,183: cond -3..19, 255, 256, int32 ends × imm19 edges, every cond 0..15 × imm19 -512..512, random | 36,674 | 10,509 | identical |
| `cbz` `cbnz` (each) | 44,848: sf × rt(0..255) × imm19 edges, random | 21,088 | 23,760 | identical |
| `adr` | 45,617: rd(0..255) × imm21 edges, imm21 -4096..4096, random | 28,737 | 16,880 | identical |
| `ldr_uoff` `str_uoff` (each) | 206,608: **all** sf × uoff(0..65535), rt × rn(0..255), 10,000 in range | 19,216 | 187,392 | identical |
| `ldrb_uoff` `strb_uoff` (each) | 141,072: **all** uoff, rt × rn(0..255), 10,000 in range | 14,866 / 14,886 | 126,206 / 126,186 | identical |
| `ldr_post` `str_post` `ldr_x_post` `str_x_post` (each) | 141,072: **all** simm9 over int16, rt × rn(0..255), 10,000 in range | 11,282-11,304 | 129,768-129,790 | identical |
| `subs_imm` | 206,608: **all** sf × imm12(0..65535), rd × rn(0..255), 10,000 in range | 19,216 | 187,392 | identical |
| `buf` | 3,201 sequences (50,436 emitter calls) into one buffer: max_len 0..40 × pos 0..max_len+8, edge fits at 4092..4096, max_len 2^63 and 2^64-1, 2,000 random | 25,130 stored, 25,306 buffer full | — | identical (every result, final pos, every byte) |
| `dec` | 462,893 words: 200,000 random; **all** 2^17 values of the 17 bits the decoder tests (mask `0xFFE07C10`), twice each with random other bits; RET and its 32 one-bit flips; each pattern, its full fill and its 32 one-bit flips | — | — | identical (44,410 decode, 418,483 invalid) |
| `val` | 2,181 buffers: every message at ml 0..100, 128, 255, 256; lengths 1..2^64-1 not a multiple of 4; every op as the last word; huge lengths stopped by a bad first word; 1,500 random programs of 1..1024 words | — | — | identical (801 valid, 1,380 refused, msg bytes 0..99) |

Every encoder record also decodes the word it wrote. Round trip over the 969,015 words: 732,171
re-encode from the decoded fields to the same word (the 14 forms whose decode carries every field),
236,844 decode to the expected op with every carried field equal (the 11 lossy forms), 0 wrong.

GNU `as` (2.42, the Spark's `/usr/bin/as`) cross-check, run by `run.sh` when `as` is there: up to 200
words per form, 4,801 in total over all 25 forms, are written back as assembly text from their fields
and assembled. Every word is identical to the .om's.

Twenty-three mutants of `aarch64.om` are part of the verdict. Each must change its stream's hash:

| mutant | records that change |
|---|---|
| add_reg opcode → AND's | add_reg 75,536 |
| rd accepted up to 32 | add_reg 1 |
| mov_reg reports `rm` as field 2 | mov_reg 242 |
| movz sf=1 shift limit 32 | movz 17,673 |
| movk hw at bit 22 | movk 55,578 |
| b accepts imm26 = 2^25 | b 1 |
| b.cond refuses cond 15 | b_cond 2,780 |
| cbz sf at bit 30 | cbz 10,597 |
| adr immlo at bit 28 | adr 21,483 |
| ldr uoff/size up to 4096 | ldr_uoff 2 |
| ldrb uoff up to 4096 | ldrb_uoff 1 |
| post-index simm9 mask 0xFF | ldr_post 5,625 |
| subs_imm sf=1 base 0xEB000000 | subs_imm 9,591 |
| buffer full at `pos + 4 >= max_len` | buf 146 |
| byte 0 of the store wrong | buf 1,742 |
| MUL test includes bit 15 | dec 8 |
| RET `rn` 0 | dec 1 |
| post-index LDR `sf` from bit 31 | dec 140 |
| b.cond `branch_imm` from bit 4 | dec 957 |
| `%08x` as `%07x` | val 82 |
| snprintf keeps `ml` bytes | val 430 |
| RET test on the second-last word | val 1,115 |
| decimal digit off by one | val 659 |

## Limits

- **Parameter types.** A value outside the C parameter's type traps 5: `sf` above 1 (bool), a
  register or shift above 255 (`uint8_t`), imm16/uoff/imm12 above 65535 (`uint16_t`), simm9 outside
  int16, imm19/imm21/imm26/cond outside int32, a decoded word above 2^32-1. The C cannot receive these
  values; it narrows them at the call.
- **Refusal.** The port does not print or abort. It returns `-(k+1)`; printing the line and aborting is
  the adapter's job (the harness does the printing).
- **Fixed arrays (ABI v1).** `a64put` stores into a 4096-byte buffer (`AARCH64_MAX_CODE_BYTES`). If a store
  would go past it (`max_len` above 4096, or `pos + 4` wrapping), the first byte at index 4096 or beyond
  traps 3. The C would write out of bounds there. Bytes below 4096 are written before the trap. Callers with smaller C buffers (128, 512, 1024) or
  larger ones need an adapter that copies; it is not written here. `a64validate` reads at most 4096
  bytes of code: a length over 4096 whose first 1024 words all decode traps 3, where the C would read
  past its caller's buffer (the product callers check `code_len <= 4096` first). Its `msg` is 256 bytes;
  `ml` above 256 traps 3 before anything happens. With `ml = SIZE_MAX` the C's glibc `snprintf` drops
  the message's last byte (end pointer wrap, depends on the buffer address), so `ml` above 256 is outside the contract.
- **NULL.** `out_dec == NULL` (C returns -1), `code == NULL` and a NULL `err_msg` cannot be expressed;
  the adapter keeps those checks.
- **Lossy decode.** The round trip is a word identity only where the C's decode carries every field
  (see above).

## NOT_RUN

- No GPU, model or serving test: this is host code.
- No `make test`/`make test-compiler`: the C files and every C caller are unchanged
  (`tests/compiler/legacy_a64_allowlist.txt` is untouched).
- The C adapters, and the removal of `aarch64_encoder.o`/`aarch64_decoder.o` through omega-bridge #8,
  are not done here.
- Only the pinned omc on Linux arm64 (the Spark) was used.
