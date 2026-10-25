# receipt.om: the EvidenceReceiptV1 reader in seeded Omega

`receipt.om` ports `src/omega_receipt.c` and `src/omega_receipt.h` to seeded Omega. These files are the
C reader of the aien-proof EvidenceReceiptV1 wire contract (VC1 stage 4). The port does what the C
does:

- parses the receipt JSON with serde's shape rules (no unknown fields, the same required and
  defaulted fields);
- applies every `canonical_bytes` validation in the C order;
- builds the canonical byte encoding;
- computes the id as BLAKE3 over those bytes;
- `verify_bytes` requires the `id` field to equal that BLAKE3 value, in either hex case.

It also covers `omega_tier_rank` and `omega_tier_satisfies`. Part of #726,
aien-dev/aien-architecture-mv#300 and #316. No C file is changed or removed by this port.

## Layout

The library is everything above the line `note ==== parity harness`. It uses no globals and no
`text`, and every array length is a literal, because function-mode units refuse `const` (error 9).
`parity/unit.py` can cut any entry point into a function-mode unit for the omega-bridge native call
ABI v1:

| entry | signature (ABI v1) | C counterpart |
|---|---|---|
| `rcparse js n wk` | `js view uchar 4194304`, `wk edit ulong 65536` → long | `omega_receipt_parse` |
| `rccanon js n wk out pos` | `out edit uchar 1049632` → ulong (total length) | `omega_receipt_canonical` + `omega_receipt_compute_id` |
| `rcverify js n wk out` | → long | `omega_receipt_verify_bytes` |
| `rctier s n wk` | → long | `omega_tier_rank` |
| `rcsat need have` | → long | `omega_tier_satisfies` |

`rcparse` and `rcverify` return 0 or -1, as the C does. A refusal is stored as three numbers:

- `wk[0]`: a reason code, 1..69, one for each distinct C message;
- `wk[1]`: a field-name index, for messages that name a field;
- `wk[2]`: the byte offset, for JSON syntax messages.

The program-mode harness renders these numbers as the exact C message text. `rccanon` streams the
canonical bytes:

- bytes `[pos, pos + 1 MiB)` go to `out[0..]`;
- the 32-byte id goes to `out[1049600..]`;
- the return value is the total length.

Canonical bytes over 1 MiB need one call per window, and each call re-encodes the whole receipt.

There is no DOM. Pass 1 checks the JSON syntax with the C reader's exact rules, errors and byte
offsets. It is iterative, because function mode refuses call cycles. Pass 2 reads fields by walking
the validated text, and decodes strings on the fly for comparisons, sorting and output. The whole
`wk` layout is documented at the top of `receipt.om`.

BLAKE3 is copied from `om/blake3/b3unit.om`, because Omegese has no `import`. The task named
`blake3.om`, but that file uses globals, so it cannot become a function-mode unit; `b3unit.om` is the
array-parameter form from the same merged work. Three locations changed; the compression code is
unchanged:

- the chaining-value stack lives in `wk` at 512 (depth 952, chunk count 953);
- the chunk is read from `out` at 1048576;
- the root is written to `out` at 1049600.

## Build and run

```
OMC=/path/to/seed/out/omc   # pinned: sha256 734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61
$OMC < om/receipt/receipt.om > receipt && chmod +x receipt
python3 om/receipt/parity/corpus.py /tmp/corp && ./receipt < /tmp/corp/rust.bin
```

The harness reads records from stdin and writes one line per record. The record format and the
line format are described in the notes at the end of `receipt.om`. Exit 65 means a short record.

## Parity

`OMEGA_OMC=$OMC [OMEGA_BRIDGE=<omega-bridge checkout>] sh om/receipt/parity/run.sh`

This command compares the .om output with **recorded** C oracle hashes in `parity/expected.txt`. It
does not build or run any C. It then applies 20 mutants to `receipt.om`. Each mutant must run to exit
0 and change its stream's sha256. With `OMEGA_BRIDGE` set, it also cuts the five entry points into
units, binds them with the pinned `native.om`, and compiles the generated C.

**Oracle.** `src/omega_receipt.c` (sha256 `88cfe0c5…1651`) and `omega_receipt.h` (`d8043519…1cbf`) at
omega-mv `acb3f15c`. Built with gcc 13.3.0 and `-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2`,
the Makefile CFLAGS that apply to these files. It is linked with the product BLAKE3
(`src/omega_blake3.c` + `src/sha256.c`, which is itself the bound `b3unit.om`). A second build with
the pre-port C BLAKE3 (`633a1735:src/omega_blake3.c`, sha256 `c89ef819…0f21`) gave byte-identical
output for every stream. The three receipts written by the Rust implementation verify to their
file-name ids. The driver is kept out of the tree.

**Corpus.** `parity/corpus.py` is deterministic. It has 9,261 records (45 MB) and is regenerated on
every run.

| stream | records | content | result |
|---|---|---|---|
| rust | 18 | the 3 Rust receipts, each with an upper-case id, compact, indented, keys reversed, and a `\u` escape | 18/18 lines identical |
| shapes | 96 | every receipt shape of `tests/test_omega_resolve.c` `t_receipts`, plus one receipt that uses every field; each as R (placeholder id), M (id computed, then verified) and M with an upper-case id | 96/96 |
| fields | 1,634 | each top-level key missing or given 17 wrong values; escapes and UTF-8 in every string; version and timestamp edges; commit, tier, result and digest forms; the full declared × observed mutation × tier matrix (648); ledger, lease and assertion shapes; production authority spellings; sorting and dedup with multi-byte and escaped strings; formatting variants | 1,634/1,634 |
| json | 3,412 | 110 handwritten syntax cases (every JSON message), 1,500 grammar-fuzz documents and a one-byte corruption of each, 300 random byte strings, a 3,000-key object and its duplicate-key twin | 3,412/3,412 |
| mutants | 4,062 | mutated serialised receipts: every byte of Rust receipt 1 with bit 0x20 flipped (1,168), every truncation of Rust receipt 2 (1,094), 600 random byte flips of the Rust receipts, and 1,200 flips or truncations of receipts built with computed ids | 4,062/4,062 |
| big | 21 | lists of 4,096 entries and 4,097 entries (each list field), 4,096 and 4,097 assertions, 4.1 MB canonical bytes (5 windows), a 1 MiB authority, 3 MB UTF-8 and 3.6 MB `\u` strings, inputs of exactly 4 MiB and 4 MiB + 1, empty input, a 2,000,000-digit timestamp, a 3 MB array | 21/21 |
| tier | 18 | `omega_tier_rank` for 17 strings and the `omega_tier_satisfies` table over -3, -1, 0..8 | 18/18 |

Every line holds:

- the parse return code, and the C error message or the canonical bytes in hex;
- the computed id;
- the verify return code, and the message or the id.

All 69 C messages are covered except three that are unreachable (see below). The mutants stream
alone has 59 receipts that still verify, 211 that parse but fail the id check, and 3,792 that are
refused.

**Mutants.** Twenty mutants: five JSON (duplicate key, depth, surrogate UTF-8, pair offset, leading
zero) and eleven receipt rules (deny unknown, schema, env_class, PASS needs an assertion, test-only
authority, canonical sort, id compare, `\f` decoding, set dedup, mutation order, u64 overflow). Four
more cover upper-case hex, the BLAKE3 root flag, the canonical window edge and the test-only tier
rule. Every one is killed.

**ABI check.** This was run outside the tree, by a /tmp driver linking the generated bindings. It ran
every stream through the five bound units and rendered messages from the reason codes. The output
hashed to `expected.txt`.

On the Spark (arm64), the whole corpus takes 8.7 s for the .om harness and about 1.5 s for the -O2
C driver.

## Where the C and the task text differ

- The task asks to port "receipt building, serialisation, parsing and verification". The C has no
  JSON writer and no receipt builder. It parses, validates, builds the canonical bytes, hashes and
  verifies. The port follows the C. "Building" in the corpus is the test's `rj_make` step (placeholder
  id, compute, patch), done by the harness's M records in the same way as the C driver.
- The C reports errors as `-1` plus a text message, with no numeric error codes. Parity therefore
  compares the return code and the full message text, including JSON byte offsets.
- Some code cannot run:
  - "a read-only run cannot claim that mutation" is unreachable in the C. At tier rank 4, any
    observed mutation with index ≥ 4 has severity ≥ 4 and is refused one line earlier.
  - The `need >= 4 && have < 4` clause of `omega_tier_satisfies` is implied by the final
    `have >= need`, so removing it changes nothing.
  - "out of memory" and "cannot compute canonical identity" occur only when the C's malloc fails.
    The port has no heap.

  The port keeps all of these as written.

## Limits

- **Struct not exposed.** The parsed `OmegaReceipt` is not returned. Callers get the return code, the
  reason, the canonical bytes and the id. `wk` holds offsets into `js`, not decoded strings.
  `omega_resolve.c` and `omega_vc_bridge.c` read struct fields: tier_rank, dependencies in input
  order, and others. An adapter therefore still needs field accessors before any C can be deleted.
  `omega_receipt_free` has no counterpart.
- **Message text.** Function-mode units cannot hold `text`, so units return reason codes. Today the
  69-message table lives only in the harness, and a C adapter would need the same table.
- **Capacities match the C.** Input is limited to 4 MiB (`OMEGA_RECEIPT_MAX_BYTES`), lists and
  assertions to 4,096 entries, and nesting to depth 6. Callers provide scratch: `wk` is 512 KiB and
  `out` is 1 MiB + 1 KiB. The harness program needs about 5.7 MiB of its 8 MiB stack.
- **Duplicate-key cost.** The duplicate-key check rescans earlier members of the object, which costs
  O(keys × object bytes). The C costs O(keys²) strcmp calls. Both are quadratic, and only
  pathological objects notice.
- **Tier names.** `rctier` compares all n bytes. The C stops at a NUL, so an adapter passes
  `strlen`.
- **Message truncation.** The C truncates messages to `errcap`. The harness and the oracle use 256,
  and no message is longer than 160 bytes.
