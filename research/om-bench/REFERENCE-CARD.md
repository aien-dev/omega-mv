# Omega (.om) reference card for om-bench v1

Checked against omega-mv 0b77e6ea, compiler seed/out/omc sha256 2c824805...029f. Both examples at the end
compile and run with that compiler.

## Shape
- One statement per line. Words are separated by spaces. No commas, semicolons, braces or indentation.
  `note TEXT` is the only comment.
- `fn NAME P1 T1 P2 T2 to TYPE` ... `end`, or `to void`. At most 6 parameters. `fn main to int` takes none;
  its return value is the exit status. `exit v` ends the program at once.
- A function with a return type must end every path with `ret v`. A line after `ret`, `break`,
  `continue` or `exit` in the same block is an error.
- A function call that returns a value cannot be a statement: bind it, `k = write 1 buf 0 n`.
- Blocks: `if C` ... `else` ... `end`; `while C bound N` ... `end` (N from 1 to 999999, at most N passes);
  `for i ulong K` ... `end` (i = 0 .. K-1); `break`; `continue`.

## Values are prefix words with fixed arity (no infix, no brackets needed)
- `add sub mul div rem and or xor shl shr` take 2 and give the operand type.
  `eq ne lt le gt ge` take 2 and give bool. `not neg` take 1.
- `add mul r r 4` means r*r+4. `eq rem n 2 0` means n mod 2 == 0.
- `and`/`or` evaluate both sides (no short circuit). Check an index bound in an outer `if`, then load.
- `get A i` reads an array element, `put A i v` writes one, `len A` is the ulong length.

## Types
- `bool uchar ushort uint ulong byte short int long` (u8 u16 u32 u64 i8 i16 i32 i64). `uchar` is a number.
- No implicit conversion: both operands, a name and its new value, an argument and its parameter must
  have the same type. Convert with the type word: `ulong k`, `uchar x`, `long u`. A conversion whose value
  does not fit traps (exit 105): mask first, `uchar and x 255`.
- A bare number takes the type of the other operand. A new name needs a typed value: `x = ulong 0`, not
  `x = 0`. `true`/`false` are bools.
- Literals are decimal only (no hex, no 'A'; write 65). Unsigned arithmetic wraps mod 2^width. Signed
  overflow traps (exit 101). Division by zero traps (102). Out-of-range index traps (103).

## Names and limits
- Variable, parameter, global, text and const names: at most 8 characters. Function names up to 63.
- A name first assigned inside a block dies at its `end`; assign it before the block to keep it.
- About 10 live names per function (hard limit 16 registers including temporaries); split big
  functions into helpers.
- Reserved, cannot be names: every type name, every operator word (`add sub mul div rem and or xor shl
  shr eq ne lt le gt ge neg not`), `get put len read write true false view edit fn to end if else while
  bound for ret void global text const free exit note new struct break continue trap own bitcast fsqrt
  fabs fmin fmax sizeof offsetof` and the host words. Pick plain names like `cnt`, `acc`, `pos`.
- `global NAME TYPE N` declares a zero-filled array visible in every function. A global can never be
  passed as an argument; functions use it by name.
- `NAME = new TYPE N v` makes a local array of N elements set to v; N must be a literal.
- `text NAME "bytes\n"` is a read-only uchar array (escapes `\n \t \\ \"` only).
- `const NAME TYPE LITERAL` names a constant.

## Input and output
- `read 0 A off n` reads at most n bytes from stdin into A starting at off. `write 1 A off n` writes n
  bytes of A starting at off to stdout. Both return a long: a count, 0 at end of input, or a negative
  error. Read in a loop until 0. Compare a write result with `long n`.
- There is no print word for numbers: convert digits into a uchar array yourself (see printu below).

## Compiler errors: `omc: line N: error K`
3 unknown name, 4 line ends early, 5 extra words, 6 bad number, 7 type mismatch, 8 number out of range,
9 word not allowed here, 10 duplicate name, 11 bare number needs a type, 12 too many live names,
13 name longer than 8, 15 missing end, 16 unknown word or syntax, 17 line after ret/break/exit,
18 missing ret, 19 global passed as argument, 20 write to text, 21 constant index out of range.

## Example 1: sum of non-negative whole numbers on stdin (verified; a '-' sign is skipped, not handled)
```
global inb uchar 65536
global outb uchar 32
fn readall to ulong
n = ulong 0
while lt n 65536 bound 65537
k = read 0 inb n sub 65536 n
if lt k 0
exit 2
end
if eq k 0
break
end
n = add n ulong k
end
ret n
end
fn printu v ulong to void
i = ulong 31
put outb i 10
x = v
while true bound 21
i = sub i 1
put outb i uchar add rem x 10 48
x = div x 10
if eq x 0
break
end
end
off = i
k = write 1 outb off sub 32 off
if ne k long sub 32 off
exit 3
end
end
fn main to int
n = readall
sum = ulong 0
cur = ulong 0
have = false
for i ulong n
c = get inb i
if and ge c 48 le c 57
cur = add mul cur 10 ulong sub c 48
have = true
else
if have
sum = add sum cur
end
cur = 0
have = false
end
end
if have
sum = add sum cur
end
printu sum
ret 0
end
```

## Example 2: uppercase stdin, then print a fixed word (verified)
```
global inb uchar 65536
text yes "changed\n"
text no "same\n"
fn readall to ulong
n = ulong 0
while lt n 65536 bound 65537
k = read 0 inb n sub 65536 n
if lt k 0
exit 2
end
if eq k 0
break
end
n = add n ulong k
end
ret n
end
fn main to int
n = readall
hit = false
for i ulong n
c = get inb i
if and ge c 97 le c 122
put inb i sub c 32
hit = true
end
end
k = write 1 inb 0 n
if ne k long n
exit 3
end
if hit
k = write 1 yes 0 len yes
else
k = write 1 no 0 len no
end
ret 0
end
```
