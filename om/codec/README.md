# om/codec: the graph codec, canonical form and validator in Omega

`codec.om` ports `src/omega_codec.c`, `src/omega_canonical.c` and `src/omega_validate.c` (with
`omega_codec.h`, `omega_canonical.h`, `omega_validate.h`, `omega_types.h`) to seeded Omega, for
aien-dev/omega-mv#726. It also ports the parts of `src/omega_core.c` that those files call: the
effect payload decode (`omega_effect_read`) and the four builders that `omega_graph_parse_text`
uses. No C file changes. Nothing is linked into the product yet.

| file | role |
|---|---|
| `codec.om` | the library (12 exported functions) and, after the `note ==== parity harness` line, a program-mode harness |
| `parity/corpus.py` | deterministic corpus generator (wire graphs, text inputs, hex-id records) |
| `parity/expected.txt` | sha256 of each output stream as the C oracle wrote it |
| `parity/run.sh` | compiles `codec.om`, regenerates the corpus, compares every stream with `expected.txt`; `--mutants` adds 21 mutants |
| `parity/unit.py` | cuts the function-mode unit of one exported function (for omega-bridge ABI v1) |

## What the C files contain (and where the task text differs)

The task text speaks of "programs/ops/operands" and "decode bytes into a program record". The C has
no program or op list. It has an `OmegaGraph` of up to 256 semantic objects. Each object has a kind,
up to 32 attributes (key and value), 64 relations (kind and target id), 32 constraints and a
1024-byte payload. The payload bytes are read through host structs (`TypePayload`, `ValuePayload`,
`OperationPayload`, `ApplyPayload`). The port follows the files:

- `omega_graph_deserialize_binary`: the `OMGG` wire format becomes a graph, and every object gets
  its semantic id.
- `omega_graph_serialize_binary`, `omega_canonical_encode`, `omega_compute_semantic_id`: the
  canonical encoding with sorted attributes, relations and constraints, and its sha256.
- `omega_validate_graph` and `omega_validate_object`: the C returns only `-1` for every failure.
  What tells the failures apart is the message the C writes into `err_msg`, so the port writes the
  same message byte for byte.
- `omega_graph_format_text` and `omega_graph_parse_text`. Both are in `omega_codec.c`, though the
  task text does not name them.
- `omega_hex_semantic_id`, `omega_parse_hex_semantic_id`, `omega_compare_semantic_id`,
  `omega_canonical_object_version`, `omega_canonical_check_header`.

**The task corpus does not exist.** The task asked for every `.omega`/`.osc`/`.bin` file under
`tests/` and `examples/` that the C decoder accepts:
- There is no `examples/` directory.
- `tests/` has 248 such files (246 `.osc` source texts and 2 unrelated `.bin`). The C decoder
  accepts none of them, because none starts with `OMGG`. The port refuses the same 248, and
  `run.sh` reports this.

The accepted graphs therefore come from two sources:
- **The product's own builders.** These are the `omegatool --dump-test-vectors` graphs, the
  physics law graph, and one graph that uses every builder. `corpus.py` rebuilds them, and
  `--check-product` checks that they are byte-equal to what the C builders write.
- **Generated graphs** (below).

## Design: a graph is a wire image plus an index

The C `OmegaGraph` is 6,648,836 bytes of fixed records. Omega has no structs that could be passed
across the ABI, no heap, and no globals in function mode. So the port never builds the struct.
Instead, a graph is two arrays:

- **W** (`uchar`, 2,097,152 bytes) is the `OMGG` wire image the graph was decoded from.
- **X** (`ulong`, 20,544) is an index. `X[0]` is the object count. Object `j` has 80 cells at
  `64 + 80 j`:
  - cbuf offset, clen, kind, attribute count, relation count, offset of the first relation,
    constraint count, payload length, payload offset, `has_id`;
  - the id as 4 big-endian words;
  - 32 attribute key offsets (each key length is the byte just before its key);
  - 32 constraint record offsets.

Every field of the C object is a function of W and X. Relations are fixed-size records, so one
offset locates all of them. `cparse` uses the C builders' record layout to write the objects it
builds into W as an `OMGG` image. Then it indexes W with the same decoder. A builder object has no
attributes, so its canonical encoding is exactly its record, and re-decoding it gives the same
object.

`sbyte` returns any byte of the C struct image of object `j`, using the aarch64 LP64 layout measured
with `offsetof`:
- `OmegaObject` is 25,972 bytes, with the payload at offset 24,948;
- multi-byte fields are host little-endian;
- padding is zero;
- objects at or past the count are zero, because the C memsets the graph.

The C reads past an object's 1024-byte payload in two places, and the port reproduces both:
- `format_text` folds `bytes[byte_len]` of a `KIND_VALUE`, with a `byte_len` of up to 65,535.
- The cycle walk reads `operands[operand_count]` through `ApplyPayload` for any `KIND_OPERATION`
  whose payload is at least 161 bytes, with an `operand_count` of up to 255.

C behaviours the port keeps:
- The decoded attribute key is a C string, so the canonical form and the sort use the key up to its
  first NUL, not `klen`.
- An object whose canonical form exceeds 4,096 bytes gets no id (`has_id` 0, id zero). It is
  invisible to id lookups, and `serialize` fails on it.
- `format_text` keeps the stale `w` when an operation's payload is shorter than an `ApplyPayload`:
  it advances `pos` without writing. The C's `w >= max_len - pos` check runs at equality.
- `parse_text` runs `strstr`/`strchr` over the whole rest of the string, not just the current line.
  It re-assigns pointers on every matching line. When the graph is full, builders return NULL, so
  `val_a` can stay NULL and `op_add` can become NULL.
- The product compiles with `-D_GNU_SOURCE` against glibc 2.39, so `strtoul` binds to
  `__isoc23_strtoul` (checked with `nm`) and accepts `0b`/`0B` binary. Overflow gives `ULONG_MAX`
  whatever the sign.
- glibc aarch64 `memcmp` returns -1/0/1 (measured), and so does `cidcmp`.

The cycle walk does not recurse, because function mode forbids call cycles. It keeps an explicit
stack of frames (object, phase, step, next j) and visits objects in the C's order. That order
includes duplicate objects with equal ids. Cycles are reachable only through the operand read past
the payload, when it lands on an id stored in the next object's attribute value. The corpus builds
such cycles on purpose.

## Oracle

- **Source.** omega-mv `acb3f15c`, sha256:
  - `40dbd8d3…e07a` omega_codec.c
  - `a15848c3…65c0` omega_canonical.c
  - `5d2472ec…c3ec` omega_validate.c
  - `e00b7830…4752` omega_core.c
  - `ea8ae1c8…ed15` sha256.c
  - `61d35344…392c` omega_om_units.inc
  - `c058a742…395c` omega_types.h
- **Build.** gcc 13.3.0 with the Makefile CFLAGS `-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE
  -O2 -Isrc`. The `.text` of each of the four codec objects is byte-identical to a `make` build of
  the same sources (`omega-blake3-abi/build`).
- **Driver.** It lives outside the tree, in `/tmp/turx/CodecPort-oracle/`: `oracle.c` sha256
  `1b0eed89…fa26` and `oracle.sh` `eb3d5918…417c`. One command rebuilds the C from a checkout's
  `src/`, regenerates the corpus with this `corpus.py`, writes the streams and compares their
  sha256 with `parity/expected.txt`:
  `sh /tmp/turx/CodecPort-oracle/oracle.sh CHECKOUT`.
- **Memory past the graph.** The driver puts the graph in a zeroed allocation with 128 KiB of
  slack after `OmegaGraph`, so the C's reads past the last object see zeros.
- **sha256.** The ids come from the product's `sha256.c`, which is itself the `om/sha256` unit. The
  port uses its own SHA-256 (the FIPS code of `tools/zero-legacy/sha256.om`). The driver also
  checks every serialized graph's sha256 against Python's hashlib.

## Parity (pinned omc `734f0a39…6b61`)

`codec.om` sha256 is `c8e93310…a2e8`. It compiles to a 95,048-byte program. The corpus is
generated on every run:
- `decode.pack`: 4,564 wire inputs, 13.6 MB;
- `text.pack`: 1,612 text inputs;
- `hexid.pack`: 6,000 records.

| stream | what it records | lines | result |
|---|---|---|---|
| decode | rc, count, `has_id` and id of every object | 4,564 (2,908 decoded) | equal to C |
| validate | rc and message (23 distinct message forms seen, including 8 cycles) | 2,908 | equal |
| serialize | rc, length, bytes, sha256 at max_len 1,049,606; rc and length at 8,192 | 2,908 | equal |
| format | rc and text at cap 4,096 and at cap 200 | 2,908 | equal |
| parse | parse_text rc, ids, then the validate, serialize and format lines of the graph it built | 6,448 | equal |
| hexid | parse_hex rc and output bytes, hex of an id, compare of two ids and of an id with itself | 6,000 | equal |
| header | object_version and check_header for all 65,536 (version, kind) pairs | 65,536 | equal |

The decode inputs are:
- the 3 product vectors;
- 57 edge cases;
- the 12 cycle graphs, built through the operand read past the payload;
- 21 overread graphs, whose `format_text` reads past the payload, one up to the end of a 256-object
  graph;
- 353 targeted graphs: every validate branch at its boundaries, a walk 200 deep, and a walk over 120
  duplicates;
- 18 boundary graphs: serialize totals 8,186 to 8,199, and format where the stale length equals the
  room left;
- 300 clean graphs and 700 structured random graphs;
- 2,000 single-byte mutants and 300 truncations of valid graphs;
- 500 random byte strings, and 300 random strings behind an `OMGG` header.

The text inputs cover:
- number forms: decimal, hex, octal, binary, sign, overflow, and `0x`/`0b` with no digits;
- C-locale whitespace, NUL inside the text, CRLF;
- graphs of 256 objects and more;
- random bytes.

**Mutants.** `run.sh --mutants` makes 21 single edits. Each one must change the stream it names,
and it must run to completion: a trap does not count as killed. All 21 are killed:
- attribute sort, key past its NUL, relation order, constraint length tie;
- klen 64, effect padding, id limit, unsigned width, value byte rounding;
- operand window offset, struct value offset, serialize length check, stale line length, value last
  8 bytes;
- binary prefix, strtoul overflow, line end, negative hex pair, memcmp sign, v1 effect code, a
  SHA-256 constant.

Reproduce from a fresh clone (with the pinned omc):

    OMEGA_OMC=/path/to/omc sh om/codec/parity/run.sh --mutants

## ABI note (bindable functions)

The pinned omc refuses `const` in function mode (error 9), so array lengths are written as numbers.
`parity/unit.py` cuts each exported function with its helpers. Every one of the 12 units compiles
in function mode. The pinned `native.om` (omega-bridge `012860db`, sha256 `2c2c53f6…3940`) accepts
them all, and its generated C compiles with `-Wall -Wextra -Werror`. Signatures:

- `omega_cdecode(r, w[2097152 B], n, x[20544])`
- `omega_cserial(r, w, x, mx, o[1049606 B])`
- `omega_cvalid(r, w, x, e[256 B])`
- `omega_cvobject(r, w, x, j, e[256 B])`
- `omega_cformat(r, w, x, cap, t[65536 B])`
- `omega_cparse(r, s[262144 B], n, w, x)`
- `omega_ccanon(r, w, x, j, cb[4096 B])`
- `omega_chexparse(r, h[65 B], o[32 B])`
- `omega_chexid(r, a[32 B], o[65 B])`
- `omega_cidcmp(r, a[32 B], b[32 B])`
- `omega_cobjver(r, k)`
- `omega_cchkhdr(r, v, k)`

There is also a check run outside the tree. A `/tmp` driver (`abi.c` sha256 `e2f31ab1…6a6f`) ran
all 7 streams through the generated bindings, and the output was byte-identical to the C oracle's.

## Limits

1. **WCAP.** `cdecode` returns `-2` instead of continuing when it would read a wire byte at or past
   2,097,152. The C has no such limit. `run.sh` shows a 2,097,174-byte graph that the C decodes
   (rc 0) and the port refuses (rc -2).
   Every graph that `serialize` can write is at most 1,049,606 bytes, and the product's callers use
   4,096 to 8,192-byte buffers.
2. **SCAP.** `cparse` returns `-2` when the input is longer than 262,144 bytes. The C has no limit.
   The string ends at its first NUL or at `n`.
3. **TCAP.** `cformat` returns `-1` for `cap` > 65,536, which the C would format. The product's
   caller uses 4,096.
4. **The error buffer is 256 bytes, and there is no length parameter.** The C callers pass 256
   (`omega_verify.c`, `omegatool.c`, `visor_verify.c`) or 160 (`visor_effect_request.c`). The
   longest message is 99 bytes (a dangling-relation message with its 64 hex digits), so the C's
   snprintf never truncates for these callers. A caller that passes fewer than 100 bytes would see
   truncation in the C and not here. An adapter for a 160-byte caller copies the message out.
5. **Reads past the object.** Inside the graph, the port reproduces the C struct image exactly.
   Past the end of `OmegaGraph`, the C reads memory it does not own (undefined behaviour), and the
   port returns zero there. That matches the oracle only because the oracle's graph sits in zeroed
   slack. Another caller's memory past the graph could differ.
6. **`omega_validate_object`.** The C also takes an object that is not in the graph. `cvobject`
   validates X's object `j`.
7. **Arguments.** `j` must be below 256 (`ccanon`, `cvobject`), or the call traps 3. W and X must
   come from `cdecode` or `cparse`: an arbitrary index can trap (out of range).
8. **`ccanon` on failure.** When the encoding exceeds 4,096 bytes, `ccanon` writes nothing. The C
   writes a prefix before it fails. No caller reads the buffer after a failure. `cserial` writes the
   same prefix as the C.
9. **Unreachable C paths are not ported:**
   - NULL-argument checks and their messages (`Null graph pointer`, `Null object pointer`);
   - `Graph object count %u exceeds maximum` (X never holds more than 256);
   - the C's `omega_graph_find_object*` and the other builders in `omega_core.c`, which are out of
     scope.

   The effect-refusal message in validate is ported but not covered: a decoded graph's effects have
   already passed the same check, and parse_text builds no effects.
10. **Platform.** Parity holds for the product's platform: aarch64 LP64 struct offsets, glibc 2.39
    strtoul/strtol/memcmp, and the C locale. It is not claimed for another libc or ABI.

## NOT_RUN

- No C adapter, no change to `src/`, no unit in `om/units.sh` or `src/omega_om_units.inc`.
- No deletion of the C objects from `libomega_gpu.a` (that goes through omega-bridge #8).
- No `make test`, no gate suite, no GPU.
