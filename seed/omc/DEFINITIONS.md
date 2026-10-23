# Seeded compiler function definitions, schema 1

This contract is `function-definitions-v1`. The existing compiler collects
function identities in its name pass and boundaries in its ordinary body pass.
It emits records only after those passes and call resolution succeed. It does
not introduce a source parser, accept unsupported language syntax, or export
global, text, struct, field, local, or parameter definitions. It runs before the
final executable-entry and ELF-output steps; an export is not an executable
qualification. Normal unframed compiler input retains its existing behavior.

All numeric frame fields are unsigned 32-bit little-endian integers. Hashes are
32 raw bytes, never ASCII hex. Offsets and lengths count exact source bytes.

## Input

| Order | Field | Constraint |
| --- | --- | --- |
| 1 | Eight bytes `OMDEF001` | Exact magic and version |
| 2 | `repo_len`, `path_len`, `revision_len`, `source_len` | Four u32 values |
| 3 | Repository identity | 1 to 128 bytes |
| 4 | Repository-relative path identity | 1 to 512 bytes |
| 5 | Pinned source revision | 40 or 64 lowercase hexadecimal bytes |
| 6 | Exact source content | 1 to 65,536 bytes |

Repository and path identities contain visible ASCII bytes 33 through 126.
They are opaque, case-sensitive identities here. The caller must supply a
canonical repository and path, select the pinned source, enforce corpus scope
and credentials, and reject aliases or traversal. Accepting the frame does not
authorize its source. No file, network, process, or credential access is added.
The total request is at most 66,264 bytes. No trailing bytes are accepted.

The five-byte prefix `OMDEF` selects framed mode while input arrives. A frame
shorter than that prefix is ordinary compiler input, so a caller must reject
any response without the complete response magic. Once that prefix is present,
malformed versions, short frames, metadata errors and limits return a structured
refusal. Reads use the compiler's existing fixed 8 MiB input storage; the
66,264-byte protocol limit is enforced after each read. The host must supply EOF
and an execution deadline. This is bounded local I/O, not a network service.

## Output

| Offset | Header field |
| --- | --- |
| 0 | Eight bytes `OMDEFR01` |
| 8 | Schema u32, exactly 1 |
| 12 | Status u32, zero on success |
| 16 | Source length u32 |
| 20 | Record count u32 |
| 24 | SHA-256 of the complete exact source, 32 bytes |

Success records follow the 56-byte header in compiler declaration order:

| Relative offset | Record field |
| --- | --- |
| 0 | Kind u32, exactly 1 for a function |
| 4 | Start u32, inclusive byte offset of the `fn` keyword |
| 8 | End u32, exclusive byte offset just after its matching closing `end` token |
| 12 | Name length u32, 1 to 63 |
| 16 | Stable symbol SHA-256, 32 bytes |
| 48 | Exact function-name bytes |

Leading indentation and trailing whitespace/newlines are outside the span.
The compiler's existing 512-function limit applies. The response upper bound
is 56 + 512 * (48 + 63) = 56,888 bytes. Consumers must also check actual lengths,
valid spans, duplicate identities, schema, count and the absence of trailing
data. An empty successful table is not produced by this compiler: sources
without supported functions receive the existing compiler error 16. A retrieval
query with no matching functions is a separate explicit no-match outcome.

The stable symbol identifier is SHA-256 of these concatenated bytes:

1. The eight ASCII bytes `OmDefId1`.
2. Repository length as LE u32, then repository bytes.
3. Path length as LE u32, then path bytes.
4. Kind as LE u32, exactly 1.
5. Name length as LE u32, then exact name bytes.

Revision, declaration order, source contents and span are excluded from the
stable identifier. Changing a function preserves its identity and changes the
source digest; deleting it removes the record. The same name in another path
or repository has another identity. Duplicate names in one source are refused
by the existing compiler. Never treat a matching stable ID as sufficient proof
of source revision, exact content, schema, or authorization.

## Refusals and trust binding

Every framed refusal has a nonzero status, zero records and exactly 56 output
bytes. Existing compiler error numbers are preserved. Additional errors are
80 for malformed framing/version/length, 81 for source or request bounds, and
82 for invalid identity metadata. Before source validation completes, the
header's length is the observed request length and the digest is zero. After
validation, it contains the source length and hash. All nonzero responses are
errors; their header contents must never be admitted as evidence. Failure to
write the complete frame exits with I/O status 2.

The trusted manifest must bind the exact source revision and source bytes,
this contract's SHA-256, compiler source SHA-256 and compiler executable
SHA-256. Those pins are outside the frame and recorded in the qualification
receipt. Schema number 1 or a stable symbol ID alone cannot establish that
binding. Source and record verification must finish before any content reaches
an agent. Compiler export does not enforce retrieval credentials or scopes.
