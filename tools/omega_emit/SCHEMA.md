# Op-list schema v2 (omega_emit input): v1 plus OSC-2 structs

You describe a program as one JSON object. `omega_emit` checks every op as it reads it and refuses
the first invalid one with a reason; a fully valid list becomes canonical OSC binary IR.

## Shape

```
{"structs": [ STRUCT, ... ], "functions": [ FUNCTION, ... ]}   "structs" optional (see Structs); 1..32 functions
FUNCTION = {"name": NAME, "params": [TYPE, ...], "ret": TYPE or "void",
            "locals": [TYPE, ...], "blocks": [ BLOCK, ... ]}
BLOCK    = [ OP, ... ]                       1 or more ops; the LAST op is br, cbr or ret, and only the last
```

- Strict JSON: no comments, no trailing commas, no extra keys, every listed key present.
- NAME: `[A-Za-z_][A-Za-z0-9_]{0,62}`, unique in the unit.
- Registers are numbered: the params are registers 0..P-1 (in order), then the locals are
  registers P, P+1, ... (in order). At most 6 params and 480 registers in total.
- TYPE: `bool`, `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, or an array reference
  `own:<elem>:<len>`, `shared:<elem>:<len>`, `mut:<elem>:<len>` (elem a scalar type, len 1..64),
  or a struct reference `own:<S>`, `shared:<S>`, `mut:<S>` (S a struct declared in `"structs"`).
  `ret` is a scalar type or `"void"` (never a reference).
- Blocks are numbered from 0; block 0 is where the function starts. A function may call only
  functions listed before it (no recursion).
- Every register is read only after it has been written on every path that reaches the read
  (params count as written). Registers may be written more than once.

## Ops

Fields are register numbers unless stated. `dst` is written; the others are read.

| op | fields | meaning |
|---|---|---|
| `const` | `dst`, `value` | dst = value (a whole decimal number that fits dst's type; `true`/`false` for bool) |
| `mov` | `dst`, `a` | dst = a: the same scalar type; or, for a reference dst, a reference to the same thing (array: same element type and length; struct: same struct): `own:` from `own:` (a move), `mut:` from `own:` or `mut:`, `shared:` from any |
| `add` `sub` `mul` `div` `rem` | `dst`, `a`, `b` | dst = a op b; dst, a, b the same integer type |
| `and` `or` `xor` | `dst`, `a`, `b` | bitwise (or logical, on bool); dst, a, b the same type |
| `shl` `shr` | `dst`, `a`, `b` | dst = a shifted by b; dst and a the same integer type, b any integer type |
| `neg` `bnot` | `dst`, `a` | dst = -a / bitwise not of a (same integer type) |
| `not` | `dst`, `a` | dst = logical not of a (bool) |
| `eq` `ne` `lt` `le` `gt` `ge` | `dst`, `a`, `b` | dst (bool) = a compared with b (a, b the same type) |
| `cast` | `dst`, `a` | dst = a converted to dst's integer type |
| `alloc` | `dst`, `init` | dst (an `own:` array) = new array, every element = init (element type); or dst (an `own:<S>` struct) = new struct, every cell zero: init is a `u64` local whose every write in the function is `const 0` |
| `release` | `arr` | give an `own:` array or `own:<S>` struct back |
| `load` | `dst`, `arr`, `index` | dst = arr[index] (dst has the element type, index any integer type) |
| `store` | `arr`, `index`, `value` | arr[index] = value (arr is `own:` or `mut:`) |
| `fload` | `dst`, `a`, `field` (a field NAME of a's struct), `index` (only for an array field) | dst = a.field, or a.field[index] for an array field (dst has the field's type, index any integer type; a any struct reference) |
| `fstore` | `a`, `field`, `value`, `index` (only for an array field) | a.field = value, or a.field[index] = value (value has the field's type; a is `own:<S>` or `mut:<S>`) |
| `call` | `func` (a function NAME), `args` (list of registers), `dst` (omit for a void callee) | dst = func(args); a reference argument binds as `mov` does |
| `trap` | `code` (1..14) | stop with that trap code (the block still ends with br, cbr or ret) |
| `br` | `to` (block number) | continue at block `to` |
| `cbr` | `cond` (bool register), `then`, `else` (block numbers) | continue at `then` if cond, else at `else` |
| `ret` | `a` (omit in a void function) | return a (the function's ret type) |

Example: `fn clamp_sub(a: u32, b: u32) -> u32`: a - b when a >= b, else 0.

```
{"functions": [
  {"name": "clamp_sub", "params": ["u32", "u32"], "ret": "u32", "locals": ["bool", "u32", "u32"],
   "blocks": [
    [{"op": "ge", "dst": 2, "a": 0, "b": 1}, {"op": "cbr", "cond": 2, "then": 1, "else": 2}],
    [{"op": "sub", "dst": 3, "a": 0, "b": 1}, {"op": "ret", "a": 3}],
    [{"op": "const", "dst": 4, "value": 0}, {"op": "ret", "a": 4}]
   ]}
]}
```

A loop is a block that branches back to an earlier block (`br` or `cbr` to a lower block number).

## Structs (OSC-2 item 2, docs/osc/OSC-2-DESIGN.md section 2)

```
STRUCT = {"name": NAME, "fields": [ FIELD, ... ]}     1..16 fields
FIELD  = {"name": NAME, "type": SCALAR}                a scalar field: 1 cell
       | {"name": NAME, "type": SCALAR, "len": N}      an array field [SCALAR; N], N 1..64: N cells
```

- `"structs"` is the one optional top-level key. Leave it out when the unit has no structs; when it is
  present it lists 1..16 structs (an empty list is refused, so every unit has one spelling). A unit
  without structs emits exactly the v1 bytes (IR version byte 1); a unit with structs emits IR
  version byte 2 with the struct table, the same bytes `oscc` writes for the same program.
- `"len"` is the one optional field key: present for an array field, absent for a scalar field.
- SCALAR is `bool`, `u8`..`u64` or `i8`..`i64`. A field is never a struct or a reference.
- Struct NAMEs are unique among the structs and are not a scalar type name or `void`; field NAMEs are
  unique within their struct. Struct names and function names are separate (they may coincide).
- Layout (OSC-2 2.3, part of the IR bytes): fields keep declaration order; every field takes whole
  8-byte cells (scalar 1, array N); the offset of a field is the sum of the cells before it (no
  padding, no reordering); a struct has 1..64 cells in total. Reordering fields changes the bytes.
- A struct is never a value: registers hold `own:<S>`, `mut:<S>` or `shared:<S>` references only; no
  struct copy, compare or return. Ownership is the array rule: `mov` and call arguments bind `own:`
  from `own:` (a move), `mut:` from `own:` or `mut:`, `shared:` from any, always the same struct.
  `fload` reads through any of the three; `fstore` needs `own:` or `mut:`.
- A struct literal `S { f: e, ... }` is: compute the field values, `const 0` into a fresh `u64`
  local Z, `alloc` dst `own:S` with init Z, then one `fstore` per cell (an array field `[e; N]` is N
  fstores with index registers holding 0..N-1). This is exactly how `oscc` lowers it
  (src/compiler/osc_lower.c ON_LET_ALLOC).
- Refusals name the place (function, block, op): an unknown struct in a TYPE, an unknown field, an
  `fstore` through `shared:`, an `index` on a scalar field, a missing `index` on an array field, a
  value or dst of the wrong type, a struct `alloc` whose init is not a `u64` local written only by
  `const 0`.

Example: `struct P { x: u32, v: [u8; 2] }` and `fn sum(p: &P) -> u32 { p.x + (p.v[1] as u32) }`.

```
{"structs": [{"name": "P", "fields": [{"name": "x", "type": "u32"}, {"name": "v", "type": "u8", "len": 2}]}],
 "functions": [
  {"name": "sum", "params": ["shared:P"], "ret": "u32", "locals": ["u32", "u64", "u8", "u32", "u32"],
   "blocks": [
    [{"op": "fload", "dst": 1, "a": 0, "field": "x"}, {"op": "const", "dst": 2, "value": 1},
     {"op": "fload", "dst": 3, "a": 0, "field": "v", "index": 2}, {"op": "cast", "dst": 4, "a": 3},
     {"op": "add", "dst": 5, "a": 1, "b": 4}, {"op": "ret", "a": 5}]
   ]}
]}
```
