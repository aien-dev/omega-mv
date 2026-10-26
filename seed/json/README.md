# seed/json: strict JSON reader and writer in seeded Omega

These units are part of omega-mv #788 (bounded HTTP/JSON for the private owner assistant). They are a
strict RFC 8259 reader and writer written in seeded Omega (`.om`). No host
implementation, generated code, heap, global or host word is used. They
are written for the compiler pinned at `befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d`
(omega-mv `19f014f2`).

Status: EXERCISED on the host (Linux AArch64, DGX Spark) on 2026-10-26. `tests/gate-json.sh` printed
`JSONGATE PASS rows=8758 mutants=5` against compiler `befb8199…a1d`; see "Evidence" below. That is
host evidence only. It is not a merge, a deployment, or qualification of any HTTP gateway.

| File | Contents |
|---|---|
| `json.om` | Reader: tokenizer and validator, tape accessors, string decoding, exact integers |
| `jsonw.om` | Writer: checked JSON construction, escaping, lexeme-preserving numbers (needs `json.om`) |
| `jsonf.om` | Optional exact binary64 conversion and shortest float text (needs both above) |
| `tests/jsdrv.om` | Test driver program |
| `tests/gen.py` | Deterministic corpus and CPython oracle (test harness only) |
| `tests/gate-json.sh` | Gate: parity, refusals, limits, mutants |

## Composition

The units contain no `om2` line, no `global`, no `text` and no host word. The caller composes
them into one OM-2 program unit:

```sh
{ printf 'om2\n'; cat seed/json/json.om seed/json/jsonw.om [seed/json/jsonf.om] seed/net/http.om app.om; } > app.full.om
nice -n 19 seed/out/omc < app.full.om > app
```

Arrays are caller-owned OM-2 unsized parameters (`view uchar *`, `edit uint *`). A global cannot be
passed as an argument, so the input, tape and output must be locals of the caller or arrays that
the caller received. The 4 MiB local-array limit of `omc` applies. For example, a 300 KiB body, a
65532-token tape and a 1 MiB writer buffer together use about 2.3 MiB.

Names: functions are `js_*`, constants `js*`. Locals are short lowercase names. A unit
composed with these files must not define functions, globals or constants whose names collide
with those names. Its own function names should also be longer than three characters.

Two limits of omc at pin `befb8199` affect composition. Both were reported to the compiler owner
(OmegaNext):

- Any `const` makes a function-mode unit fail with error 9 at end of input. OmegaNext reports the
  fix as omega-mv PR #806. Until it lands, the library compiles only into a program (`fn main`);
  the gate checks it with an empty `main`.
- A `const` defined after any `fn` makes a later signature that names an earlier const as an
  array length fail with error 16. Composed after `jsonf.om`, a unit that defines constants must
  therefore not write `fs edit long jsfs` in its own signatures. Pass the array to the `js_*`
  functions from the function that declares it (`fs = new long jsfs 0`), or write the length 2464
  directly, as `tests/jsdrv.om` does.

## Limits

| Limit | Value | Where |
|---|---|---|
| Input bytes per parse | 1 .. 983040 (`jsmaxn`), chosen per tape at `js_init` | refusal 5 |
| Nesting depth | 1 .. 65535 (`jsdcap`), chosen at `js_init` | refusal 4 |
| Tokens | `(len tape - 16) / 4`, at most 16777215 | refusal 11 |
| Writer nesting | 1024 | writer refusal 25 |
| Writer output | `len ob - 1040` bytes | writer refusal 21 |

983040 bytes is 960 KiB: Wolfram `2^20*15/16 -> 983040`. Every loop has a bound that a proof
can justify. For example, each parser step uses at least one distinct input byte, so the steps
number at most n + 1 <= 983041 < 999999.

## Reader API (`json.om`)

```
js_init  tape edit uint * slim ulong dlim ulong to long      0, or -13 (bad limits, tape < 20 cells)
js_parse src view uchar * tape edit uint * n ulong to long   token count, or -code (tape then empty)
```

`js_init` sets up the 16-cell header and the limits once. After that, `js_parse` may be called
on the same tape any number of times. It parses `src[0 .. n)` as exactly one JSON text,
optionally surrounded by whitespace. After a refusal, `js_err` and `js_epos` give the code and
the byte position, and no token is visible.

Accessors take `tape view uint *` and return 0, or `jsnone` (4294967295), on a refused or
uninitialised tape or an out-of-range token:

| Function | Result |
|---|---|
| `js_ok tape` | bool: the last parse succeeded |
| `js_ntok tape` | token count |
| `js_err tape`, `js_epos tape` | refusal code and byte offset |
| `js_maxdep tape` | deepest nesting seen |
| `js_dtotal tape`, `js_dmax tape` | decoded bytes of all keys and strings, and of the longest one |
| `js_type tape t` | 1 `{`, 2 `}`, 3 `[`, 4 `]`, 5 key, 6 string, 7 number, 8 true, 9 false, 10 null |
| `js_off tape t`, `js_span tape t` | source byte offset and length. A container spans from its bracket to its closing bracket; a string includes its quotes; a number is its lexeme |
| `js_match tape t` | for a bracket, the index of the matching bracket |
| `js_nkids tape t` | members of an object or elements of an array |
| `js_slen tape t` | decoded byte length of a key or string |
| `js_nflags tape t` | number flags: 1 minus sign, 2 fraction, 4 exponent |
| `js_last tape t` | last token of the value at `t` |
| `js_first tape t` | first key or element of a non-empty container, else `jsnone` |
| `js_mnext tape k` | the key after member key `k`, else `jsnone`. The value of key `k` is `k + 1` |
| `js_enext tape e` | the element after array element `e`, else `jsnone` |

Each token occupies 4 cells starting at 16 + 4t. Cell 0 holds the type, the flags shifted left
by 4 and the child count shifted left by 8. Cell 1 is the offset, cell 2 the span, and cell 3
holds the match for a bracket or the decoded length for a string. Tokens appear in source
order. Duplicate member names are kept, in order. RFC 8259 section 4 leaves their meaning to
the application.

Strings from the source:

```
js_sget  src view uchar * off ulong ob edit uchar * to long   decode the string at src[off]; bytes or -code (-13 short ob)
js_streq src view uchar * off ulong key view uchar * to bool  decoded string equals key bytes exactly
```

`js_slen`, or `js_dmax` for every string at once, gives the buffer size needed. Decoding
writes UTF-8: escapes are resolved and a surrogate pair becomes one 4-byte code point.

Integers are exact. Nothing is ever rounded silently:

```
js_ilex src off   0 not an integer lexeme, 1 integer with |v| <= 2^64-1, 2 larger
js_isu  / js_tou  fits ulong (no minus sign) / its value (0 when not)
js_isl  / js_tol  fits long / its value (0 when not); "-0" is 0
```

Any other number keeps its lexeme. A caller can copy it, compare it, or write it back
unchanged (`js_wlex`). Conversion to binary64 is an explicit opt-in (`jsonf.om`).

## Reader refusals

`js_parse` returns minus the code. `js_epos` is the byte offset where the refusal was found.

| Code | Name | Meaning |
|---|---|---|
| 1 | `jseeof` | input ended inside a value (also empty input) |
| 2 | `jsesyn` | unexpected byte, including a byte-order mark, a trailing comma, a missing `:` |
| 3 | `jsetrl` | bytes after the root value |
| 4 | `jsedep` | an opening bracket would exceed the depth limit |
| 5 | `jsesiz` | n is over the size limit (position = the limit) |
| 6 | `jseutf` | ill-formed UTF-8 inside a string (RFC 3629 section 4: overlong, surrogate, > U+10FFFF, stray or missing continuation) |
| 7 | `jsesur` | a `\u` surrogate that is not one high-then-low pair |
| 8 | `jsectl` | a raw byte below 0x20 inside a string |
| 9 | `jseesc` | an unknown escape or a non-hex `\u` digit |
| 10 | `jsenum` | number grammar (leading zero, `1.`, `-`, `1e`) |
| 11 | `jsetok` | the tape is full |
| 12 | `jselit` | a misspelled `true`, `false` or `null` |
| 13 | `jsearg` | bad arguments (n > len src, tape not initialised, limits out of range) |

Outside strings only the four RFC whitespace bytes and the grammar's ASCII are accepted, so
any non-ASCII byte there is refused as code 2. Strings are validated during the parse, and
`js_sget` checks them again: it never trusts the offset it is given.

## Writer API (`jsonw.om`)

The caller passes one `ob edit uchar *` array. Its first 1040 bytes are the writer state, and
the JSON text is `ob[1040 .. 1040 + js_wlen ob)`.

```
js_winit ob                       reset, 0 or -26 (ob <= 1040 bytes)
js_wobj / js_wobje / js_warr / js_warre ob
js_wnull ob, js_wbool ob b, js_wulong ob v, js_wlong ob v
js_wnumb ob s off n               s[off..off+n) must be exactly one RFC 8259 number lexeme; copied unchanged
js_wlex  ob src off               the lexeme of a parsed number token at src[off], unchanged
js_wsbuf ob s off n               a string from raw UTF-8 bytes (validated, escaped)
js_wsbeg ob / js_wsb ob c / js_wsend ob   the same, streamed one byte at a time
js_wsrc  ob src off               re-encode the parsed key or string at src[off]
js_wtok  ob src type off          write one parsed token (rewriting a whole tape in order reproduces the value)
js_wdone ob                       text length if exactly one complete value was written, else -code
js_wlen ob, js_werr ob
```

A string is written as a member key when the innermost open container is an object that is
waiting for a key. Otherwise it is written as a value. Commas and colons are inserted
automatically. Every call checks the state, so the writer can only produce RFC 8259 text.
The first refused call records its code, and every call after it writes nothing.

Escaping follows RFC 8259 section 7 exactly as CPython `json.dumps(..., ensure_ascii=False)`
does. `"` and `\` are escaped. `\b \f \n \r \t` use their short forms. The other bytes below
0x20 become `\u00xx` with lowercase hex. Everything else, including `/`, DEL and all non-ASCII
UTF-8, is copied unchanged.

| Code | Name | Meaning |
|---|---|---|
| 21 | `jswesp` | output array full |
| 22 | `jswest` | call not valid in this state (a value where a key is due, a second root, a key without a value, a mismatched close, an unfinished string, nothing written) |
| 23 | `jsweut` | ill-formed UTF-8 in string bytes |
| 24 | `jswenm` | not a number lexeme; infinity and NaN, which JSON cannot represent |
| 25 | `jswedp` | nesting over 1024 |
| 26 | `jswearg` | bad array or slice |
| 27 | `jswesrc` | `js_wsrc` offset is not a well-formed source string |

## Optional floats (`jsonf.om`)

```
js_f64   src off fs   -> f64     nearest binary64, ties to even; overflow +-inf, underflow +-0
js_f64st fs           -> ulong   0 finite, 1 overflowed to infinity, 2 no number lexeme at off (NaN returned)
js_fmt64 fs x         -> long    shortest round-trip text at fs[2416 ..], or -1 for inf/NaN
js_fmtch fs i         -> uchar   byte i of that text
js_wf64  ob x fs                 write it (inf/NaN refused with 24)
```

`fs` is a caller `long` array of `jsfs` = 2464 cells, about 19 KiB. The method is the
arbitrary-precision decimal of Go `strconv`: `decimal.go` with its shifts, `atof.go` `floatBits`,
and `ftoa.go` `roundShortest`. It uses 800 digits plus a truncation flag and is exact for every
lexeme, including more than 800 significant digits and exact halfway cases. The text uses
Python `repr` layout. Fixed notation is used when the decimal exponent x satisfies -4 <= x < 16,
with `.0` added to whole numbers. Otherwise the layout is `d.ddde+XX`.

This is deliberately **not** the numeric contract of `serde_json` 1.0.151. Its default float
parser is not correctly rounded. Cortex's `/tmp/cxo/cortex/om/f64.om` replicates that behavior
for Cortex parity and needs about 230 MB of pools. Neither its numeric contract nor its
allocation is used here.

## Tests

```sh
nice -n 19 sh seed/json/tests/gate-json.sh
```

The gate needs Linux AArch64, python3 >= 3.11, and the pinned `seed/out/omc`. If any of these
is missing, it prints `JSONGATE NOT_RUN` and exits 3. It runs these steps:

1. It checks that the library has no `global`, `text`, `om2` line or host word, and that it
   compiles on its own with an empty `main`.
2. It builds the driver `tests/jsdrv.om`. `tests/gen.py` writes a deterministic corpus (seed 788)
   containing the following inputs:
   - hand-written valid and invalid cases;
   - 39 refusals whose code and position are pinned (36 listed in `PINNED`, plus depth, capacity and gateway size);
   - the size and depth boundaries, both inclusive;
   - token capacity;
   - a 307200-byte (300 KiB) gateway chat request, which must be accepted at limit 307200 and
     refused at 307199;
   - about 1,900 float lexemes, including exact binary64 midpoints, midpoints plus or minus
     10^-900 relative (more than 800 digits), subnormals and overflow;
   - 700 random documents with random escaping and whitespace;
   - 900 byte-level mutants of them;
   - 150 surrogate-escape documents;
   - writer scripts.
3. Every parse case runs in four modes. The two token modes, the flat token stream and the walk
   through the navigation API, must both match CPython `json.loads`. That parse uses the C
   scanner with `parse_int`/`parse_float` hooks receiving exact lexemes and `object_pairs_hook`
   to keep order and duplicates. Every span, match, flag and decoded length is also self-checked.
   The f64 mode must match CPython `float()`, `repr()` and its bits, and the integer accessors
   must match `int()`. The rewrite must be byte-identical to a serializer built from
   `json.encoder.c_encode_basestring` with lexemes kept. Writer scripts must match pinned bytes
   and refusal codes.
4. A driver input larger than its array exits 93.
5. Five mutants must each be caught:
   - an overlong E0 sequence accepted;
   - raw 0x1F accepted;
   - the depth limit off by one;
   - 0x1F written unescaped;
   - halfway cases rounded up instead of to even.

The oracle differs from CPython in four documented places. CPython accepts all of them, and RFC
8259 or the limits require refusing them:
- lone surrogate escapes (RFC 8259 section 8.2: strings must be Unicode text);
- `NaN`, `Infinity` and `-Infinity`, which CPython accepts by default (`parse_constant` refuses);
- depth over the limit;
- size or tape capacity over the limit.

CPython is a read-only referee. It never writes bytes that are compiled.

Driver modes and the writer-script language are documented at the top of `tests/jsdrv.om`. A
writer script has one operation per line:

- `o O a A`: object or array open and close;
- `s<hex>`: a string from bytes;
- `S`, `b<hh>`, `E`: a streamed string;
- `n<lexeme>`: a number lexeme;
- `u<dec>`, `l<dec>`: unsigned and signed integers;
- `t f z`: true, false, null;
- `d<16 hex>`: f64 bits.

Numeric constants were checked with WolframLanguageEvaluator:
- `{2^64-1, Quotient[2^64-1,10], Mod[2^64-1,10], 2^63}` -> `{18446744073709551615, 1844674407370955161, 5, 9223372036854775808}`;
- surrogate bounds `FromDigits[{"D800","DBFF","DC00","DFFF"},16]` -> `55296, 56319, 56320, 57343`;
- UTF-8 lead and continuation bytes `80 C2 E0 ED F0 F4 A0 9F 90 8F BF` -> `128 194 224 237 240 244 160 159 144 143 191`;
- `2^52-1 -> 4503599627370495`, `2^53 -> 9007199254740992`;
- the quiet NaN `FromDigits["7FF8000000000000",16] -> 9221120237041090560`;
- the scratch layout `16+3*800 -> 2416`, `+48 -> 2464`.

## Evidence

This run was on 2026-10-26, on Linux aarch64 (DGX Spark host), with Python 3.12.3. The tree was
omega-mv `58d6fb6a2f72b1ab02c0eb0c01afe141303c968d` with these units uncommitted on top.

```
ionice -c3 nice -n 19 sh seed/json/tests/gate-json.sh   -> exit 0
COMPILER befb8199db8c4bca188a494dfaac2a6f0ee5c36fcea11188c75666c55fce8a1d
SRC 456254fc754ea35103ced480407b24fc1a3896d094b90ae0e9623e7b6594e2d2 seed/json/json.om
SRC 464f649e8207495f5d67fe8795d31e42ea3ccebd9ed547274dab802d4f7c5f21 seed/json/jsonw.om
SRC bade33cef4b7edcc834c436d8499810aa573e02a97fb0dafded3736744e67a29 seed/json/jsonf.om
SRC 2f2f8ac522db876db55bca464060ab6e1071447180f51c4e53051d4ce3ddd710 seed/json/tests/jsdrv.om
gen.py 6e8169151790a07e1cca57505e0bbdce729b5b8db9ab121adbd2c0041f835985
DRIVER 15e928704eb5be7f078712c77e536940053541e245018472d58d9f9259c13778
GEN cases=2764 rows=8758
JSONGATE PASS rows=8758 mutants=5
```

Of the 2003 parse cases checked in mode `t`, 1094 were compared byte for byte and 909 were
refusals checked by their `ERR ` prefix. Pinned refusals are compared exactly. All five mutants
made the parity run fail. If any of the hashes above change, this record no longer applies; rerun
the gate.
