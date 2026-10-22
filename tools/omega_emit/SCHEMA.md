# Op-list schema v1 (omega_emit input)

You describe a program as one JSON object. `omega_emit` checks every op as it reads it and refuses
the first invalid one with a reason; a fully valid list becomes canonical OSC binary IR.

## Shape

```
{"functions": [ FUNCTION, ... ]}            1..32 functions
FUNCTION = {"name": NAME, "params": [TYPE, ...], "ret": TYPE or "void",
            "locals": [TYPE, ...], "blocks": [ BLOCK, ... ]}
BLOCK    = [ OP, ... ]                       1 or more ops; the LAST op is br, cbr or ret, and only the last
```

- Strict JSON: no comments, no trailing commas, no extra keys, every listed key present.
- NAME: `[A-Za-z_][A-Za-z0-9_]{0,62}`, unique in the unit.
- Registers are numbered: the params are registers 0..P-1 (in order), then the locals are
  registers P, P+1, ... (in order). At most 6 params and 480 registers in total.
- TYPE: `bool`, `u8`, `u16`, `u32`, `u64`, `i8`, `i16`, `i32`, `i64`, or an array reference
  `own:<elem>:<len>`, `shared:<elem>:<len>`, `mut:<elem>:<len>` (elem a scalar type, len 1..64).
  `ret` is a scalar type or `"void"`.
- Blocks are numbered from 0; block 0 is where the function starts. A function may call only
  functions listed before it (no recursion).
- Every register is read only after it has been written on every path that reaches the read
  (params count as written). Registers may be written more than once.

## Ops

Fields are register numbers unless stated. `dst` is written; the others are read.

| op | fields | meaning |
|---|---|---|
| `const` | `dst`, `value` | dst = value (a whole decimal number that fits dst's type; `true`/`false` for bool) |
| `mov` | `dst`, `a` | dst = a: the same scalar type; or, for an array dst, an array of the same element type and length: `own:` from `own:` (a move), `mut:` from `own:` or `mut:`, `shared:` from any |
| `add` `sub` `mul` `div` `rem` | `dst`, `a`, `b` | dst = a op b; dst, a, b the same integer type |
| `and` `or` `xor` | `dst`, `a`, `b` | bitwise (or logical, on bool); dst, a, b the same type |
| `shl` `shr` | `dst`, `a`, `b` | dst = a shifted by b; dst and a the same integer type, b any integer type |
| `neg` `bnot` | `dst`, `a` | dst = -a / bitwise not of a (same integer type) |
| `not` | `dst`, `a` | dst = logical not of a (bool) |
| `eq` `ne` `lt` `le` `gt` `ge` | `dst`, `a`, `b` | dst (bool) = a compared with b (a, b the same type) |
| `cast` | `dst`, `a` | dst = a converted to dst's integer type |
| `alloc` | `dst`, `init` | dst (an `own:` array) = new array, every element = init (element type) |
| `release` | `arr` | give an `own:` array back |
| `load` | `dst`, `arr`, `index` | dst = arr[index] (dst has the element type, index any integer type) |
| `store` | `arr`, `index`, `value` | arr[index] = value (arr is `own:` or `mut:`) |
| `call` | `func` (a function NAME), `args` (list of registers), `dst` (omit for a void callee) | dst = func(args) |
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

## GPU kernels (IR encoding version 6, docs/omegese/GPU.md)

A function with `"kind": "kernel"` and `"threads": N` (32..1024, a multiple of 32) runs once per GPU
thread. It returns `"void"`, takes at most 32 params, and is never called. Its key order is `name`,
`kind`, `threads`, `params`, `ret`, `locals`, `blocks`. Extra types, kernels only:

- `f32` (IEEE binary32). A `const` of f32 takes a decimal number (rounded once to binary32) or a
  string `"0x"` + 8 hex digits (the bits).
- `gview:<elem>` / `gedit:<elem>` (elem `u32` or `f32`): a global buffer the kernel reads / reads and
  writes. Each is a param followed by its `u32` length param.
- `block:<elem>:<len>` (len 1..12288): a shared array, made by `shared`.

| op | fields | meaning |
|---|---|---|
| `thread` `block` `blocks` `batch` `batches` | `dst` | dst (u32) = this thread's index in its block / the block index along x / blocks along x / the block index along y / blocks along y |
| `shared` | `dst` | dst (a `block:` array) = a zeroed shared array; entry block only |
| `gload` | `dst`, `buf`, `index` | dst = buf[index] (bounds against buf's length param, trap 3) |
| `gstore` | `buf`, `index`, `value` | buf[index] = value (buf is `gedit:`) |
| `sync` | none | every thread of the block waits here |
| `down` | `dst`, `a`, `b` | dst = a from the lane b higher in the warp (b a `const` 1..31; a lane with none gets its own a) |
| `min` `max` | `dst`, `a`, `b` | smaller / larger (u32 or f32) |
| `exp` `inv` `rsq` `sqrt` | `dst`, `a` | f32 approximations: e^a, 1/a, 1/sqrt(a), sqrt(a) (GPU.md section 5) |
| `mad` | `dst`, `a`, `b`, `c` | dst = a * b + c, f32, rounded once |

In a kernel `add` `sub` `mul`, `neg` and the comparisons also take f32, `cast` converts between u32 and
f32, `load` and `store` take `block:` arrays, and `trap` does not end the path (the thread records the
code and stops storing; only `ret` ends it). `alloc`, `release`, `call` and the 64-bit and signed
types are refused in a kernel (checker2, rules C2-GPU-TYPE and C2-GPU-CALL, GPU.md section 10).
