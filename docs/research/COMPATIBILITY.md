# Omega and every other language: the door design

Status: design only, 2026-10-22 (UTC), ledger L409. No code. The build that follows it is ledger L410. Companion study:
docs/research/LANGUAGES.md (what each language got right and wrong; decisions 26 and up). Outside facts carry [n];
the sources are listed at the end with the sentence copied from each page. Statements about Omega's own code name
the file they come from.

## Words used here, defined once

- **ABI** (application binary interface): the rules for how compiled code calls other compiled code: which processor
  register holds which argument, how memory is laid out, how a library names what it offers. Two programs written
  in different languages can call each other if both follow the same ABI, whatever language they were written in.
- **C ABI**: the ABI that C compilers follow on a given machine. It is named after C, but it is a written rule book
  published by the chip and operating-system makers, not a piece of C code.
- **Register**: one of the processor's small built-in storage slots (on the Spark's Arm cores, x0 to x30 for whole
  numbers and v0 to v31 for floating-point and vector values) [1, 2].
- **Shared object (.so)**: a file of compiled code that another running program can load and call into. On Linux
  the file format is ELF.
- **Symbol / export**: a name in a shared object that outside programs may look up and call.
- **FFI** (foreign function interface): a language's built-in way to call a function compiled from another
  language.
- **Door**: in this document, the entry code at the start of every Omega function that outsiders may call. It checks
  what the caller handed in before any Omega code runs.
- **Buffer**: a run of bytes in memory, given as a start address plus a length.
- **Shape and stride**: for a multi-dimensional array, the shape is the size along each dimension (for example
  4 rows by 8 columns); the stride is how far to step in memory to reach the next element along a dimension.
- **Oracle**: a trusted outside program used only to compare answers, never to produce Omega's bytes.
- **ELF fields** (ET_DYN, PT_DYNAMIC, .dynsym, DT_HASH, DT_SONAME, DT_NEEDED): labelled entries inside an ELF file
  that tell the loader "this is a library", where its table of exported names is, how to look names up quickly,
  the library's own name, and which other libraries it needs.
- **Position-independent code**: machine code that works wherever in memory the loader places it.
- **Stack / stack pointer**: the scratch memory a function uses while it runs, and the register that marks its
  current end. **Red zone**: on x86-64 Linux, a 128-byte area just past the stack pointer that a function may use
  without moving the pointer.
- **Descriptor**: a small record that describes an array (where it starts, its shape, its strides) without holding
  the data itself.
- **Unwinding / panic**: when a language aborts a function on error, it walks back up through the callers
  (unwinding); a panic is Rust's and Go's word for such an abort.
- **Atomic instruction**: a processor instruction that reads and changes one memory value in a single step, so two
  threads cannot both win.
- **Row-major / column-major**: two orders for storing a table in memory, row after row (C, Omega) or column after
  column (Fortran).
- **Blittable** (C#): a type whose bytes look the same in .NET and in C, so it can cross without conversion.
- **Digest**: a short fingerprint computed from bytes; change any byte and the fingerprint changes.
- **Fixpoint**: the point where running a translator or compiler again produces exactly the same output as last time.
- **Negative control**: a test that feeds in something known to be wrong and must see it refused, to prove the
  check can fail.
- **Level-1 vector routines**: the simplest standard linear-algebra routines (add two vectors, scale a vector, dot
  product), from the BLAS library family.

## In one page

Every major language already has a way to call functions that follow the platform C ABI. Python, Ruby, Rust, Go,
Java, JavaScript (Node), Julia, Fortran, C#, Swift, Zig, Haskell and PHP all do; Lua and OCaml do it through their C host interfaces (section 5). The sources below
show each one. So the shortest route to "every language can call Omega, and Omega can call them" is not a bridge
per language. It is one door: Omega's compiler (omc, being built under L408) writes ordinary ELF shared objects
whose exported functions follow the C ABI exactly. Every language then reaches Omega through the door it already
has.

The door carries a strict rule set so that the mistakes other languages made at this border do not come inside.
Every buffer comes with its length; overlapping buffers are refused; Omega never keeps a caller's pointer after it
returns; errors come back as a number, never as a crash or a jump out of the function; the floating-point rounding
mode is set at the door so results are the same whoever calls. Inside the door, decisions 16 to 24 hold as they do
for any Omega program.

The C ABI is a set of rules, and omc writes the machine words that follow those rules itself. Nothing from gcc, the
C library or the system linker enters Omega's bytes. Other languages appear only as callers in tests, the same way
gcc appears today: as an oracle.

What it costs Omega: one more output format in omc (a shared object, not only a program), a door prologue per
exported function, and a written table of the ABI rules. What it does not cost: no garbage collector, no runtime
library, no per-language glue inside Omega.

## 1. Why the C ABI is the common door

**Every language already has one.** Haskell's designers wrote one standard foreign interface, treating C as the common base,
after each compiler had its own [3], and it specifies only the C calling convention [4]. Rust's own reference
says the Rust calling convention carries no stability promise [5]; a Rust team proposal for a richer cross-language
ABI says that today other languages still meet Rust at the C ABI [6], and that proposal is still experimental
[7]. Zig's author states the strategy directly: follow the C ABI so other languages can call Zig [8]. Mojo calls
and is called through the C ABI [9], and reaches C++ only through `extern "C"` [10]. Swift is adding an `@c` attribute
for the same purpose [11].

**Language-specific binary interfaces have been expensive.** Stroustrup calls a stable C++ binary interface a hard
problem and part of why people ask for C interfaces [12]; he counts this among the reasons people need C, Java or C# interfaces to C++ programs [13].
Swift's stable ABI arrived with Swift 5, only on Apple platforms first [14, 15], and its manifesto describes the
version lock that existed before it [16]. Node's native add-ons had to be rebuilt for every major Node release
until Node-API gave them a fixed C interface [17, 18]. Lua's change of its C interface in version 4.0 stranded the
code of the lab where Lua was born [19]. Java's own team calls its native interface (JNI) brittle [20]: three
separate pieces to keep in sync [21] and no mapping of Java types to C types [22], gaps that third-party libraries
grew up to fill [23].

**What Omega takes from this.** Omega does not invent an Omega-to-Omega binary interface for other languages to
learn. The C ABI is the only binary promise Omega makes to the outside, and Omega keeps it exactly. Between Omega
units, imports resolve by digest (decision 21) and the C ABI is not involved.

## 2. The seed rule: rules, not C code

The C ABI is a document. On the Spark it is Arm's "Procedure Call Standard for the Arm 64-bit Architecture"
(AAPCS64) plus the ELF format; on x86-64 machines it is the System V AMD64 ABI plus ELF. Each says which registers
carry what, how the stack is aligned and how a shared object lists its names. omc follows those rules by writing the
right machine words itself, the same way the seed chain's assembler (asm2, L103) writes AArch64 words from a text
table. Concretely:

1. The ABI rules omc needs are written as a text table in the Omega tree (register roles, alignment, ELF header and
   dynamic-section fields), each row quoting the line of the Arm or ELF document it comes from. That table is what
   omc reads, the same way asm2 reads its instruction table.
2. omc writes the ELF shared object byte by byte, as seed0 writes its ELF header today. No system linker (ld), no
   assembler (as), no gcc, no C library.
3. The shared object names no libraries it depends on (no DT_NEEDED entries [24]). Omega code inside calls nothing
   outside itself until the outward-call rung (C8 below), so the file depends on nothing.
4. Python, Rust, Go, Ruby, Node, Fortran and the rest appear only as callers in tests, outside the Omega path,
   named in the evidence each time, as gcc is named today when it is used as an oracle.

## 3. What omc must emit

### 3.1 The calling convention (AAPCS64 on the Spark)

| rule | source | what omc does |
|---|---|---|
| integer and pointer arguments in x0 to x7, results back in the same registers | [25] | exported functions take at most 8 whole-number or address arguments; more is a compile-time refusal (the same limit OSC-4 already designs for outward calls, EXTERN_ARITY, docs/osc/OSC-4-DESIGN.md section 3.2) |
| floating-point arguments in v0 to v7 | docs/osc/OSC-4-DESIGN.md:565 (checked there against the Arm document) | same limit of 8 |
| x19 to x29 are callee-saved: a called function must restore them | [26] | the door saves any of these it uses and restores them before returning |
| the stack pointer must be a multiple of 16 at any public entry | [27] | omc keeps every frame a multiple of 16 bytes (OSC already does: docs/osc/OSC-4-DESIGN.md section 3.2 item 2) |
| results larger than registers go to caller memory addressed by x8 | [28] | not used: every export returns one status number in x0 and writes results into buffers the caller passes |
| structs over 16 bytes are passed by hidden pointer; small float structs travel in vector registers | [29, 30] | not used: no struct is passed or returned by value; descriptors are passed by address |

Keeping exported signatures to whole numbers, floats and addresses avoids struct passing and the floating-point aggregate rule [29, 30], the parts of the ABI we judge most
likely to differ between language FFIs (our reading). It is the subset every FFI in section 5
handles.

### 3.2 The shared object

| field | source | what omc writes |
|---|---|---|
| ELF type: shared object, with a PT_DYNAMIC program header pointing at the dynamic section | [31] | yes |
| dynamic symbol table (.dynsym) holding only what is exported | [32] | exactly the functions marked for export, as global functions |
| everything else hidden | [33, 34] | every other function and every pool is a local symbol or no symbol at all |
| symbol hash table (DT_HASH), string table, symbol table | [35] | yes, so the system loader can find the names |
| DT_SONAME, the library's own name | [36] | `lib<unit>.so.<interface digest>`: the name changes whenever an exported signature changes, because without symbol versioning a new name is the only safe way to ship an incompatible interface [37] |
| DT_NEEDED, libraries it depends on | [24] | none |

The code is position-independent: it reaches its own data with program-counter-relative addressing, so the loader
can place it anywhere without patching code.

### 3.3 Data at the border

**Scalars.** Omega's explicit widths (decision 20) map one to one: u8..u64 and i8..i64 are the C ABI's 8- to
64-bit whole numbers; bool crosses as one byte holding 0 or 1, and the door refuses any other value.

**Buffers.** A buffer always crosses as two arguments: the address, then a u64 length. This is decision 16 at the
border, and it is the rule OSC-4 already wrote for calls going out ("Every slice parameter of an `extern fn` must be
followed immediately by an explicit `u64` length parameter", docs/osc/OSC-4-DESIGN.md section 3.1). An address with
no length cannot be declared. Java's replacement for JNI made the same choice from the other side: a raw pointer that
arrives from C is treated as a zero-length region until code deliberately attaches a size [38].

**Shaped arrays.** A multi-dimensional array crosses as the address of a descriptor laid out exactly as DLPack's
`DLTensor`, a descriptor that gives the data address, the shape and the strides, with strides counted in elements, not bytes [39].
DLPack is the small C struct that PyTorch, NumPy, JAX and other array libraries already use to hand arrays to each
other [40, 41]. Choosing its layout means those libraries can pass arrays to Omega with no Omega-specific code. Rules
the door applies:

- DLPack strides count elements, while the Python buffer protocol and NumPy's older interface count bytes [39, 42, 43].
  The door works in DLPack's units only; a bridge from the buffer protocol converts before the call.
- Strides may be negative in the buffer protocol [44]. The first version of the door accepts only the dense row-major
  layout (strides equal to what the shape implies) and refuses anything else with an error number. Strided views come
  later, when a real caller needs them, and each new layout gets its own gate.
- DLPack asks producers to align data to 256 bytes, but several large frameworks did not, and DLPack's own header records that
  [45], so the door does not rely on it. It checks only element alignment and refuses a misaligned element.
- Ownership never crosses. In DLPack the producer keeps owning the memory [46]; the Arrow format has the same rule,
  with a release callback the consumer calls [47]. Omega only borrows the array for the length of the call and never
  calls a deleter.

### 3.4 Errors as values

Every exported function returns one u32 in x0: 0 for success, otherwise an Omega error number or trap code.
Results go into buffers the caller passed in. Nothing else signals an error:

- **No unwinding across the door.** In Rust a panic crossing a plain `extern "C"` boundary aborts the process, and a
  foreign exception entering Rust that way is undefined behaviour [48, 49]. Lua's C interface raises errors with
  `longjmp`, which skips the C code in between [50]. JNI leaves a pending exception that native code must clear
  before it may call most functions [51]. Omega has no exceptions, so a trap inside Omega ends the call and returns
  its code through the door. The calling process keeps running.
- **No side channel.** C's `errno` is only meaningful after a call reports failure [52] and is per-thread [53]; Go
  has to offer a two-result calling form to fetch it [54]. Omega's status is the return value itself.
- **No hardware surprises.** On AArch64 the divide instruction returns 0 for a zero divisor instead of stopping, and
  the most negative number divided by minus one wraps [55, 56]. Decision 19 already requires a defined trap for
  division by zero; omc emits the test before every divide, and the door reports the trap code to the caller.
- **No signals.** Omega code never asks the operating system to stop the process. A foreign host never sees a
  signal caused by Omega.

## 4. The rules at the border

A foreign caller can hand Omega things that decisions 16 to 24 forbid inside Omega. The door checks what can be
checked, refuses with an error number, and the document says plainly what cannot be checked.

| what a caller can break | evidence it happens | what the door does | what the door cannot know |
|---|---|---|---|
| **overlap**: two buffers sharing memory | memcpy forbids overlap and calls it undefined [57]; a glibc speed-up in 2.13 changed the copy order and broke programs that had overlapped by luck [58]; C's `restrict` is an unchecked promise [59]; Fortran forbids changing an argument through a second name so compilers can keep values in registers [60] | for every buffer Omega writes, compares its address range against every other buffer argument; any overlap is refused (decision 18). With at most 8 arguments that is at most 28 comparisons | nothing: overlap of the passed ranges is fully decidable at the door |
| **bounds**: a length longer than the real memory | ctypes warns a wrong declaration can corrupt memory or crash the interpreter [61]; Node says the same of its FFI [62] | refuses a null address, a length whose byte size overflows 64 bits, a length that does not equal the shape's element count, and a misaligned element | whether the memory behind the address really is that long. No FFI can check this; it is the caller's promise, written into the interface description |
| **lifetime**: memory freed while Omega uses it, or a pointer kept after return | Go's rule: C may not keep a Go pointer after the call [63], checked only at run time [64]; Julia's garbage collector does not see raw pointers, so the caller must keep the object alive [65]; Ruby FFI callers must know who owns each buffer [66] | Omega never stores a foreign address beyond the call and never returns an address into its own memory. Results are copied into caller buffers | the caller must keep its memory alive until the call returns. Every call is synchronous, so that is the whole window |
| **threads**: two threads entering at once | Python's global lock blocks multi-core use, and removing it broke binary compatibility for every extension [67, 68]; Ruby gives each Ractor its own lock [69] | Omega's fixed pools (decision 24) are a workspace whose size omc knows at compile time. The workspace size is exported as a constant; the caller passes the workspace memory on each call. The door marks the workspace in use with one atomic instruction and refuses a second entry on the same workspace with a BUSY error. Functions with no workspace need nothing | nothing more: two threads with two workspaces run independently |
| **rounding mode**: the caller changed how floats round | on AArch64 the rounding mode is a control-register field that applies to every scalar and vector float instruction [70] | the door saves the floating-point control register, sets round-to-nearest, and restores the caller's value on exit, so results are bit-identical whoever calls | nothing |
| **stack**: a caller thread with a small stack | none sourced | omc computes each export's deepest stack use at compile time (exports may not recurse without a stated bound) and records it in the interface description | the caller's real stack size; recorded as a documented requirement |

### The interface description

A ctypes caller has to write each signature by hand, and ctypes cannot check it against the real function [71].
cffi's authors prefer the mode that checks declarations against the real header with a compiler [72]. The
WebAssembly component model made a separate interface language (WIT) for the same reason: plain numbers cross easily,
richer types need an agreed description [73]. For every shared object, omc also writes a plain-text interface
description: one line per export with its name, argument widths, buffer and descriptor arguments, workspace size,
stack use and error numbers. Bindings for each language are generated from that file, never typed by hand, so a
declaration cannot drift from the function. Each export also gets a companion symbol `<name>__sig` returning a
64-bit digest of its signature; a generated binding checks it once at load and refuses to run on a mismatch.

## 5. Every language, through its own door

Two kinds of callers exist.

**Direct callers** load a shared object and call a C-ABI function with no glue in between. They need nothing from
Omega except the door (rungs C1 to C7):

| language | its door to the C ABI | what to watch for |
|---|---|---|
| Python | ctypes, or cffi in its no-compiler mode [71, 72, 74, 75] | signatures typed by hand and unchecked [71, 61]: use the generated binding |
| Rust | `extern "C"` with `#[no_mangle]` on its own side; `#[repr(C)]` for any struct [76, 77, 78] | none beyond the door rules |
| Go | cgo, by importing the pseudo-package "C" [79] | cgo turns off cross-compiling by default and breaks Go's script-free build [80, 81]; the pointer rules [63, 64] match the door's lifetime rule |
| Julia | `ccall` with no glue code; arrays of plain numbers share C and Fortran layout [82] | keep arrays alive during the call [65] |
| Fortran | ISO_C_BINDING and BIND(C), standard since Fortran 2003 [83] | Fortran arrays are column-major and count from 1 [84]; the generated binding passes the shape so the descriptor states the order |
| Java | the Foreign Function and Memory API, whose memory segments carry spatial and temporal bounds [85] | raw pointers arrive with size zero [38]; the binding attaches the length from the interface description |
| C# | P/Invoke; blittable types cross untouched [86] | Omega's whole-number and address types are blittable; a true-or-false value is converted to 1, 2 or 4 bytes [87], so the generated binding states one byte |
| Ruby | the ffi gem [66] | ownership must be explicit [66]; Omega never hands out memory, so there is none to free |
| JavaScript (Node) | `node:ffi`, built in since Node 26.1.0 and marked experimental [88] | Node's own warning about bad pointers [62]; Node-API remains the stable route (below) |
| PHP | the FFI extension since PHP 7.4 [89, 90] | none beyond the door rules |
| Swift | `@c` functions and C imports through Clang [11, 91] | none beyond the door rules |
| Zig | `extern` and `export` with C-ABI layout [92] | none beyond the door rules |
| Haskell | the standard FFI, C calling convention only [93, 4] | Omega never calls back, so the cheaper "unsafe" import is allowed [93] |
| Kotlin/Native | cinterop, generated from C headers; still labelled Beta [94, 95] | omc writes a C-style header text from the interface description for tools that need one; it is a description, not code Omega uses |

**Host-API callers** expect a native module to call back into the host's own C interface: CPython extension modules
(with the stable ABI, a fixed set of functions promised binary-compatible across Python 3 [96]), Node-API add-ons
[97], Lua modules that use the Lua stack [98], JNI libraries [51], OCaml stubs [99] and Erlang native functions,
where a crash takes down the whole virtual machine [100]. These need Omega to call outward: to call functions the host process provides. OSC-4's Slice C design
(docs/osc/OSC-4-DESIGN.md section 3) already lays out outward C-ABI calls for the OSC compiler: a static allowlist
table, no dlopen, arguments in x0 to x7 and v0 to v7. Rung C8 brings that design to omc. Until then, Python reaches
Omega directly through ctypes and Node through `node:ffi`; neither needs Omega to call back.

## 6. Bringing other languages' code in

Calling is one direction. The other is moving existing code into Omega. The Go team did this for its own compiler:
it wrote a translator from C to Go that was built for that one codebase, not as a general tool, and the team
ran it repeatedly, comparing the new compiler's output with the old until it was bit-identical [101]. Their design
document argues that a mechanical translator makes consistent mistakes that are easy to find, where a person makes
subtle ones [102]. The hard part was types: C uses one pointer type where Go tells pointers and slices apart, so the
translator had to work out from how each value was used which one was meant [103]. Other translators made different
trades: c2rust produces Rust that mirrors the C and is unsafe on purpose, leaving safety to later work [104]; f2c
aimed for bit-level compatibility with the native Fortran compiler and accepted ugly C [105], and its convention of
passing every argument by address with hidden string lengths still shapes Fortran interop today [106].

**The Omega rule.** A translator into Omegese is a tool outside the Omega path, like an oracle. Its output must pass
checker2 like any Omega program, so the translator has to produce lengths for every buffer and prove every pair of
buffers disjoint, or refuse. It never produces unsafe Omega, because Omegese has no unsafe form. The gate for every
translated program is the Go gate: same inputs, same outputs as the original, over a fixed corpus.

**Which languages first, and why.**

1. **Fortran.** Its arrays carry shape, and its rule against changing an argument through a second name [60] is
   decision 18 already. Numerical Fortran (dense linear algebra, the kind of code a model runtime uses) maps onto
   Omega's decisions with the fewest refusals. First target: a small set of level-1 vector routines.
2. **C.** The largest body of systems and numeric code, and Omega's own oracle sources are C. The translator meets
   Go's pointer-versus-slice problem [103] head on: every C pointer must become a buffer with a length or be refused.
   Expect many refusals; each one is counted and published.
3. **CUDA C++ kernels.** The GPU kernels Omega runs today are produced by a kernel builder written in C
   (docs/OMEGA.md section 1). Translating the kernels themselves into Omegese GPU words is bar C1 of docs/OMEGA.md;
   the gate is identical results on the chip.
4. **Not translated: Python, Ruby, JavaScript, PHP, Lua.** Their meaning depends on dynamic types and a garbage
   collector, so a faithful translation would have to bring a runtime with it. These languages call Omega through
   the door instead.

**Translator gates (design, no code).** T1: every program in the corpus gives identical outputs translated and
original. T2: checker2 accepts every translated program. T3: every refusal is listed with its reason and counted
against programs attempted. T4: a one-word mutation of the translator breaks at least one corpus row (the gate can
fail).

## 7. The rung list for the build

L410 builds rungs C1 to C7 once L408's omc reaches its fixpoint (rung R5). Each rung commits its evidence under
evidence/COMPAT/<rung>/ with a negative control that must fail.

| rung | what | gate | needs L408's omc? |
|---|---|---|---|
| C0 | the ABI rules table: AAPCS64 register roles and the ELF fields of section 3, as a text table omc reads; each row quotes its source line | every row carries a quoted line from the Arm or ELF document; a reviewer finds 0 rows without one | no |
| C1 | omc writes an ELF shared object with one exported function and no library dependencies | the system loader finds the export when Python's ctypes loads the file (test caller); a listing tool used as an oracle shows exactly the exported names and no DT_NEEDED; negative control: a function not marked for export cannot be found | yes (R5) |
| C2 | the scalar corpus: the 10 dev tasks compiled as exports | for every corpus input, the value Python receives through ctypes equals the Omega-native run (raw result and trap code); table committed; negative control: a one-word change in one task makes its row differ | yes |
| C3 | errors as values | a trap inside an export (division by zero, a loop past its bound) returns its trap code to the caller and the calling process keeps running; division by zero never returns 0; negative control: the same call with a non-zero divisor returns the right value | yes |
| C4 | buffers | byte copy, sum and a 1000-entry sort through (address, length) pairs equal the native run; overlapping buffers are refused with an error number while the same call on disjoint buffers succeeds; a null address and a length whose byte size overflows are refused | yes |
| C5 | every direct caller | the C2 and C4 corpus called from Rust, Go (cgo), Ruby (ffi gem), Node (`node:ffi`), Fortran (BIND(C)) and Julia gives the same table as Python and as the native run, row for row; bindings generated from the interface description; a binding with a wrong signature digest refuses to load | yes |
| C6 | shaped arrays | a NumPy and a PyTorch array passed through a DLPack descriptor give the native result; a non-dense stride, a wrong element type and a misaligned element are each refused | yes, plus structs (R6) |
| C7 | threads and rounding | two threads with two workspaces give the same results as one thread alone; two threads on one workspace get exactly one BUSY refusal; a caller that sets round-toward-zero before the call gets bit-identical float results (once omc has floats) | yes |
| C8 | Omega calls out: host-API modules | a CPython extension module using only the stable ABI, a Node-API add-on and a Lua module, each written in Omega, give the C2 table; Slice C's allowlist rule holds (an unlisted function cannot be called) | yes, plus outward calls |
| C9 | the second ABI: System V x86-64 | the C2 table on an x86-64 machine (the iMac test computer) with a second rules table: six integer argument registers [107], separate float registers [108], the 128-byte red zone respected [109] | yes, plus an x86-64 back end (not planned yet) |
| T1 to T4 | translators, Fortran first | section 6 | no for the design; yes to run translated programs |

## 8. What this design does not do

- It does not make Omega safe against a caller that lies about a length. No FFI can; the door checks everything the
  arguments themselves reveal, and the interface description states the rest as the caller's obligation.
- It does not offer callbacks (Omega calling a function pointer the caller passes in). Callbacks bring back
  re-entry and lifetime problems (our reasoning); they wait for a real need.
- It does not promise a C++, Rust or Swift native binary interface. Those languages meet Omega at the C ABI, as they
  meet each other.
- It does not cover the GPU. Arrays on the GPU cross as DLPack descriptors; running Omega
  GPU kernels for a foreign caller waits for bar C1 of docs/OMEGA.md.

## Sources

Every source below was fetched on 2026-10-22 (UTC) and its quoted sentence re-found in the fetched text by a script (re-fetch, strip markup, compare words). Format: [n] URL, title, date, the sentence copied from the page.

[1] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "In a 64-bit context these registers are normally referred to using the names x0-x30"

[2] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "The Arm 64-bit architecture also has a further thirty-two registers, v0-v31, which can be used by SIMD and Floating-Point operations."

[3] https://www.microsoft.com/en-us/research/wp-content/uploads/2016/07/history.pdf, "A History of Haskell: Being Lazy with Class", 2007. Quote: "This so-called Foreign Function Interface (FFI) treats C as a lowest common denominator: once you can call C you can call practically anything else."

[4] https://www.haskell.org/onlinereport/haskell2010/haskellch8.html, "Haskell 2010 Language Report, Chapter 8: Foreign Function Interface", 2010. Quote: "The Haskell FFI currently only specifies the interaction between Haskell code and foreign code that follows the C calling convention."

[5] https://doc.rust-lang.org/stable/reference/items/external-blocks.html, "External blocks - The Rust Reference", n.d. Quote: "This is the default when a function is declared without using extern fn. The Rust ABI offers no stability guarantees."

[6] https://github.com/rust-lang/compiler-team/issues/631, "Experimental feature gate for extern "crabi" ABI (major change proposal #631)", 2023-05-09. Quote: "often have to use the C ABI as a lowest-common-denominator for cross-language function calls"

[7] https://github.com/rust-lang/compiler-team/issues/631, "Experimental feature gate for extern "crabi" ABI (major change proposal #631)", 2023-05-09. Quote: "There will need to be a full RFC before this can be stabilized"

[8] https://ziglang.org/learn/why_zig_rust_d_cpp/, "Why Zig When There is Already C++, D, and Rust?", n.d. Quote: "Zig is attempting to become the new portable language for libraries by simultaneously making it straightforward to conform to the C ABI for external functions"

[9] https://docs.modular.com/mojo/faq, "Mojo FAQ (Modular)", n.d. Quote: "Mojo code is interoperable with C code."

[10] https://docs.modular.com/mojo/faq, "Mojo FAQ", n.d. Quote: "Mojo code is also interoperable with C++ code that uses extern "C""

[11] https://raw.githubusercontent.com/swiftlang/swift-evolution/main/proposals/0495-cdecl.md, "SE-0495: C compatible functions and enums", n.d. Quote: "That function uses the C calling convention and its signature can only reference types representable in C."

[12] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "ABI stability for C++ is a genuinely hard technical problem, especially as the C++ standard has to be platform independent."

[13] https://www.stroustrup.com/hopl20main-p5-p-bfc9cd4--final.pdf, "Thriving in a Crowded and Changing World: C++ 2006-2020", 2020-06. Quote: "I consider this a contributing reason that people require C, Java, C#, etc., interfaces to programs written in C++."

[14] https://www.swift.org/blog/abi-stability-and-more/, "ABI Stability and More", 2019-02-07. Quote: "Swift 5 provides binary compatibility for apps: a guarantee that going forward, an app built with one version of the Swift compiler will be able to talk to a library built with another version."

[15] https://www.swift.org/blog/abi-stability-and-more/, "ABI Stability and More", 2019-02-07. Quote: "ABI stability is implemented for each operating system that it compiles and runs on."

[16] https://raw.githubusercontent.com/swiftlang/swift/main/docs/ABIStabilityManifesto.md, "Swift ABI Stability Manifesto", n.d. Quote: "Without source compatibility, projects face version-lock where all source code in a project and its packages must be written in the same version of Swift."

[17] https://nodejs.org/en/learn/modules/abi-stability, "ABI Stability (Node.js Learn)", n.d. Quote: "the application has to be recompiled, reinstalled, and redeployed whenever a new major version of Node.js is introduced into the production environment."

[18] https://nodejs.org/api/n-api.html, "Node-API (Node.js documentation)", n.d. Quote: "It is intended to insulate addons from changes in the underlying JavaScript engine and allow modules compiled for one major version to run on later major versions of Node.js without recompilation."

[19] https://www.lua.org/doc/hopl.pdf, "The Evolution of Lua", 2007. Quote: "Tecgraf, Lua’s birthplace, never upgraded from Lua 3.2 to Lua 4.0 because of the big changes in the API."

[20] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "Replace the brittle machinery of native methods and the Java Native Interface (JNI) with a concise, readable, and pure-Java API."

[21] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "JNI involves several tedious artifacts: a Java API (native methods), a C header file derived from the Java API, and a C implementation that calls the native library of interest."

[22] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "JNI does not reconcile the Java type system with the C type system."

[23] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", n.d. (delivered in Java 22). Quote: "Over the years, numerous frameworks have emerged to fill the gaps left by JNI, including JNA, JNR and JavaCPP."

[24] https://www.sco.com/developers/gabi/latest/ch5.dynamic.html, "Dynamic Section (System V gABI, Chapter 5)", n.d. Quote: "This element holds the string table offset of a null-terminated string, giving the name of a needed library."

[25] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "The first eight registers, r0-r7, are used to pass argument values into a subroutine and to return result values from a function."

[26] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "Registers r19-r29 and SP are Callee-saved"

[27] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "SP mod 16 = 0. The stack must be quad-word aligned."

[28] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "The address of the memory block shall be passed as an additional argument to the function in x8."

[29] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "If the argument type is a Composite Type that is larger than 16 bytes, then the argument is copied to memory allocated by the caller and the argument is replaced by a pointer to the copy."

[30] https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst, "Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)", n.d. Quote: "A Homogeneous Floating-point Aggregate (HFA) is a Homogeneous Aggregate with a Fundamental Data Type that is a Floating-Point type and at most four uniquely addressable members."

[31] https://www.sco.com/developers/gabi/latest/ch5.pheader.html, "Program Header (System V gABI, Chapter 5)", n.d. Quote: "A PT_DYNAMIC program header element points at the .dynamic section"

[32] https://www.sco.com/developers/gabi/latest/ch4.sheader.html, "Section Header (System V gABI, Chapter 4)", n.d. Quote: "Consequently, an object file may also contain a SHT_DYNSYM section, which holds a minimal set of dynamic linking symbols, to save space."

[33] https://www.sco.com/developers/gabi/latest/ch4.symtab.html, "Symbol Table (System V gABI, Chapter 4)", n.d. Quote: "A symbol defined in the current component is hidden if its name is not visible to other components."

[34] https://www.akkadia.org/drepper/dsohowto.pdf, "How To Write Shared Libraries", 2011-12-10. Quote: "Symbols marked like this are not exported from the DSO and therefore cannot be used from other objects."

[35] https://www.sco.com/developers/gabi/latest/ch5.dynamic.html, "Dynamic Section (System V Application Binary Interface, gABI)", n.d. Quote: "This element holds the address of the symbol hash table, described in"

[36] https://www.sco.com/developers/gabi/latest/ch5.dynamic.html, "Dynamic Section (System V gABI, Chapter 5)", n.d. Quote: "This element holds the string table offset of a null-terminated string, giving the name of the shared object."

[37] https://www.akkadia.org/drepper/dsohowto.pdf, "How To Write Shared Libraries", 2011-12-10. Quote: "Making the change and still maintaining ABI stability requires the use of versioning."

[38] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", 2024-10-07. Quote: "The FFM API represents a pointer returned from a foreign function as a zero-length memory segment"

[39] https://raw.githubusercontent.com/dmlc/dlpack/main/include/dlpack/dlpack.h, "dlpack.h (DLPack header)", n.d. Quote: "strides of the tensor (in number of elements, not bytes)"

[40] https://dmlc.github.io/dlpack/latest/, "Welcome to DLPack's documentation (DLPack)", n.d. Quote: "Already a standard with wide community adoption and support: NumPy CuPy PyTorch Tensorflow MXNet TVM mpi4py Paddle JAX Hidet"

[41] https://dmlc.github.io/dlpack/latest/, "DLPack documentation", n.d. Quote: "In order for an ndarray system to interact with a variety of frameworks, a stable in-memory data structure is needed."

[42] https://docs.python.org/3/c-api/buffer.html, "Buffer Protocol (Python C API)", n.d. Quote: "Contrary to most data types exposed by the Python interpreter, buffers are not PyObject pointers but rather simple C structures."

[43] https://numpy.org/doc/stable/reference/arrays.interface.html, "The array interface protocol (NumPy Manual)", n.d. Quote: "a tuple of strides which provides the number of bytes needed to jump to the next array element in the corresponding dimension"

[44] https://docs.python.org/3/c-api/buffer.html, "Buffer Protocol (Python C API)", n.d. Quote: "For example, with negative strides the value may point to the end of the memory block."

[45] https://raw.githubusercontent.com/dmlc/dlpack/main/include/dlpack/dlpack.h, "dlpack.h (DLPack header)", n.d. Quote: "TVM, perhaps others) do not adhere to this 256 byte alignment requirement"

[46] https://dmlc.github.io/dlpack/latest/python_spec.html, "Python Specification for DLPack", n.d. Quote: "The producer keeps owning the memory of x (and y if a copy is made)"

[47] https://arrow.apache.org/docs/format/CDataInterface.html, "The Arrow C data interface (Apache Arrow documentation)", n.d. Quote: "Resource lifetime management through a custom release callback."

[48] https://doc.rust-lang.org/nomicon/ffi.html, "Foreign Function Interface (The Rustonomicon)", n.d. Quote: "or foreign (e.g. C++) exceptions to cross an FFI boundary, that boundary must use the appropriate -unwind ABI string."

[49] https://rust-lang.github.io/rfcs/2945-c-unwind-abi.html, "RFC 2945: C-unwind ABI", 2019-04-03. Quote: "panic! will cause an abort if it would otherwise “escape” from a function defined with"

[50] https://www.lua.org/manual/5.4/manual.html, "Lua 5.4 Reference Manual", n.d. Quote: "Internally, Lua uses the C longjmp facility to handle errors."

[51] https://docs.oracle.com/en/java/javase/21/docs/specs/jni/design.html, "JNI Design Overview (Java SE 21 JNI Specification)", n.d. Quote: "After an exception has been raised, the native code must first clear the exception before making other JNI calls."

[52] https://man7.org/linux/man-pages/man3/errno.3.html, "errno(3) Linux manual page", n.d. Quote: "The value in errno is significant only when the return value of the call indicated an error"

[53] https://man7.org/linux/man-pages/man3/errno.3.html, "errno(3) Linux manual page", n.d. Quote: "errno is thread-local; setting it in one thread does not affect its value in any other thread."

[54] https://pkg.go.dev/cmd/cgo, "cgo command (pkg.go.dev)", n.d. Quote: "Any C function (even void functions) may be called in a multiple assignment context to retrieve both the return value (if any) and the C errno variable as an error"

[55] https://devblogs.microsoft.com/oldnewthing/20220801-00/?p=106922, "The AArch64 processor (aka arm64), part 5: Multiplication and division (The Old New Thing)", 2022-08-01. Quote: "If you try to divide by zero, there is no exception. The result is just zero."

[56] https://devblogs.microsoft.com/oldnewthing/20220801-00/?p=106922, "The AArch64 processor (aka arm64), part 5: Multiplication and division (The Old New Thing)", 2022-08-01. Quote: "There is also no exception for dividing the most negative integer by −1. You just get the most negative integer back."

[57] https://man7.org/linux/man-pages/man3/memcpy.3.html, "memcpy(3) Linux manual page", n.d. Quote: "The memory areas must not overlap. Use memmove(3) if the memory areas do overlap."

[58] https://man7.org/linux/man-pages/man3/memcpy.3.html, "memcpy(3) Linux manual page", n.d. Quote: "Most notably, in glibc 2.13 a performance optimization of memcpy () on some platforms (including x86-64) included changing the order in which bytes were copied from src to dest ."

[59] https://en.cppreference.com/w/c/language/restrict, "restrict type qualifier (cppreference)", n.d. Quote: "To avoid undefined behavior, the programmer must ensure that the aliasing assertions made by the restrict-qualified pointers are not violated."

[60] https://j3-fortran.org/doc/year/18/18-007r1.pdf, "J3/18-007r1: Fortran 2018 Interpretation Document (working draft)", 2018-10-09. Quote: "Action that affects the value of the entity or any subobject of it shall be taken only through the dummy argument unless"

[61] https://docs.python.org/3/library/ctypes.html, "ctypes: A foreign function library for Python", n.d. Quote: "Calling a function with an incorrect calling convention or with incorrect arguments can corrupt memory or crash Python."

[62] https://nodejs.org/api/ffi.md, "FFI (Node.js API documentation)", n.d. Quote: "Passing invalid pointers, using an incorrect symbol signature, or accessing memory after it has been freed can crash the process or corrupt memory."

[63] https://pkg.go.dev/cmd/cgo, "cgo command (pkg.go.dev)", n.d. Quote: "C code may not keep a copy of a Go pointer after the call returns, unless the memory it points to is pinned with runtime.Pinner and the Pinner is not unpinned while the Go pointer is stored in C memory."

[64] https://pkg.go.dev/cmd/cgo, "cgo command (pkg.go.dev)", n.d. Quote: "The default setting is GODEBUG=cgocheck=1, which implements reasonably cheap dynamic checks."

[65] https://docs.julialang.org/en/v1/manual/calling-c-and-fortran-code/, "Calling C and Fortran Code (Julia manual)", n.d. Quote: "Whenever you have created a pointer to Julia data, you must ensure the original data exists until you have finished using the pointer."

[66] https://github.com/ffi/ffi/wiki/Pointers, "Pointers (ffi/ffi Wiki)", n.d. Quote: "Some C libraries will return you a pointer to “fresh” memory. That means you’re responsible for freeing that memory after you’re finished with it."

[67] https://peps.python.org/pep-0703/, "PEP 703, Making the Global Interpreter Lock Optional in CPython", 2023-01-09. Quote: "The GIL is an obstacle to using multi-core CPUs from Python efficiently."

[68] https://peps.python.org/pep-0703/, "PEP 703, Making the Global Interpreter Lock Optional in CPython", 2023-01-09. Quote: "CPython builds without the GIL will not be ABI compatible with the standard CPython build or with the stable ABI due to changes to the Python object header needed to support biased reference counting."

[69] https://docs.ruby-lang.org/en/master/Ractor.html, "Ractor (Ruby documentation)", n.d. Quote: "On CRuby (the default implementation), the Global Virtual Machine Lock (GVL) is held per ractor, so ractors can run in parallel."

[70] https://df.lth.se/~getz/ARM/SysReg/AArch64-fpcr.html, "FPCR, Floating-point Control Register (mirror of Arm's AArch64 system register description)", n.d. Quote: "The specified rounding mode is used by both scalar and Advanced SIMD floating-point instructions."

[71] https://docs.python.org/3/library/ctypes.html, "ctypes: A foreign function library for Python", n.d. Quote: "ctypes cannot determine a function’s calling convention or signature."

[72] https://cffi.readthedocs.io/en/stable/overview.html, "CFFI Overview (ABI versus API)", n.d. Quote: "If using a C compiler to install your module is an option, it is highly recommended to use the API mode instead."

[73] https://component-model.bytecodealliance.org/design/why-component-model.html, "Why the Component Model? (Component Model documentation)", n.d. Quote: "For WebAssembly modules written in different languages to interoperate smoothly, there needs to be an agreed-upon way to expose these richer types across module boundaries."

[74] https://docs.python.org/3/library/ctypes.html, "ctypes, A foreign function library for Python", n.d. Quote: "It provides C compatible data types, and allows calling functions in DLLs or shared libraries."

[75] https://cffi.readthedocs.io/en/stable/overview.html, "CFFI documentation, Overview (ABI versus API)", n.d. Quote: "The most immediate drawback of the ABI level is that calling functions needs to go through the very general libffi library, which is slow (and not always perfectly tested on non-standard platforms)."

[76] https://doc.rust-lang.org/nomicon/ffi.html, "Foreign Function Interface (The Rustonomicon)", n.d. Quote: "The no_mangle attribute turns off Rust’s name mangling, so that it has a well defined symbol to link to."

[77] https://doc.rust-lang.org/nomicon/ffi.html, "Foreign Function Interface (The Rustonomicon)", n.d. Quote: "Rust guarantees that the layout of a struct is compatible with the platform’s representation in C only if the #[repr(C)] attribute is applied to it."

[78] https://doc.rust-lang.org/nomicon/ffi.html, "Foreign Function Interface - The Rustonomicon", n.d. Quote: "Rust guarantees that the layout of a struct is compatible with the platform"

[79] https://pkg.go.dev/cmd/cgo, "cgo command - cmd/cgo - Go Packages", n.d. Quote: "Cgo enables the creation of Go packages that call C code."

[80] https://dave.cheney.net/2016/01/18/cgo-is-not-go, "cgo is not Go", 2016-01-18. Quote: "By default cgo is disabled when cross compiling."

[81] https://dave.cheney.net/2016/01/18/cgo-is-not-go, "cgo is not Go", 2016-01-18. Quote: "the source of your program contains enough information for a tool to build the project"

[82] https://arxiv.org/pdf/1411.1607, "Julia: A fresh approach to numerical computing", 2015-07-07. Quote: "one can take a pointer to the first array element and pass it to a C library function using ccall and it will just work"

[83] https://snapshots.sourceware.org/gcc/docs/latest/gfortran/Interoperability-with-C.html, "Interoperability with C (The GNU Fortran Compiler)", n.d. Quote: "Since Fortran 2003 (ISO/IEC 1539-1:2004(E)) there is a standardized way to generate procedure and derived-type declarations and global variables that are interoperable with C"

[84] https://gcc.gnu.org/onlinedocs/gfortran/Interoperability-with-C.html, "Interoperability with C (The GNU Fortran Compiler)", n.d. Quote: "Note that array dimensions are reversely ordered in C and that arrays in C always start with index 0 while in Fortran they start by default with 1."

[85] https://openjdk.org/jeps/454, "JEP 454: Foreign Function & Memory API", 2024-10-07. Quote: "All memory segments provide spatial and temporal bounds which ensure that memory access operations are safe."

[86] https://learn.microsoft.com/en-us/dotnet/framework/interop/blittable-and-non-blittable-types, "Blittable and Non-Blittable Types (Microsoft Learn)", n.d. Quote: "Structures that are returned from platform invoke calls must be blittable types."

[87] https://learn.microsoft.com/en-us/dotnet/framework/interop/blittable-and-non-blittable-types, "Blittable and Non-Blittable Types (.NET, Microsoft Learn)", n.d. Quote: "System.Boolean Converts to a 1, 2, or 4-byte value with true as 1 or -1."

[88] https://nodejs.org/api/ffi.md, "FFI (Node.js API documentation)", n.d. (added in v26.1.0). Quote: "The `node:ffi` module provides an experimental foreign function interface for loading dynamic libraries and calling native symbols from JavaScript."

[89] https://wiki.php.net/rfc/ffi, "PHP RFC: FFI - Foreign Function Interface", 2018-12-04. Quote: "Proposal It is proposed to extend PHP with a simple FFI API designed after LuaJTI/FFI and Python/CFFI (the latter was actually based on former)."

[90] https://wiki.php.net/rfc/ffi, "PHP RFC: FFI - Foreign Function Interface", 2018-12-04. Quote: "in pure PHP, without having to have deep knowledge in the Zend extension API, and without having to learn a 3rd “intermediate” language."

[91] https://raw.githubusercontent.com/swiftlang/swift/main/docs/CppInteroperability/CppInteroperabilityManifesto.md, "C++ Interoperability Manifesto (Swift repository)", n.d. Quote: "Swift/C++ interoperability builds on top of the Swift/C interoperability, so it helps to be familiar with"

[92] https://ziglang.org/documentation/master/, "Documentation (The Zig Programming Language)", n.d. Quote: "An extern struct has in-memory layout matching the C ABI for the target."

[93] https://www.haskell.org/onlinereport/haskell2010/haskellch8.html, "Haskell 2010 Language Report, Chapter 8: Foreign Function Interface", 2010. Quote: "A safe call is less efficient, but guarantees to leave the Haskell system in a state that allows callbacks from the external code."

[94] https://kotlinlang.org/docs/native-c-interop.html, "Interoperability with C - Kotlin Documentation", n.d. Quote: "Kotlin/Native comes with a cinterop tool, which you can use to quickly generate everything you need to interact with an external C library."

[95] https://kotlinlang.org/docs/native-c-interop.html, "Interoperability with C - Kotlin Documentation", n.d. Quote: "The C libraries import is in Beta. All Kotlin declarations generated by the cinterop tool from C libraries should have the @ExperimentalForeignApi annotation."

[96] https://peps.python.org/pep-0384/, "PEP 384: Defining a Stable ABI", 2009-05-17. Quote: "This PEP proposes to define a stable set of API functions which are guaranteed to be available for the lifetime of Python 3, and which will also remain binary-compatible across versions."

[97] https://nodejs.org/api/n-api.html, "Node-API (Node.js documentation)", n.d. Quote: "This API will be Application Binary Interface (ABI) stable across versions of Node.js."

[98] https://www.lua.org/manual/5.4/manual.html, "Lua 5.4 Reference Manual", n.d. Quote: "Lua uses a virtual stack to pass values to and from C."

[99] https://ocaml.org/manual/5.2/intfc.html, "Interfacing C with OCaml (OCaml manual)", n.d. Quote: "All OCaml objects are represented by the C type"

[100] https://www.erlang.org/doc/apps/erts/erl_nif.html, "erl_nif (Erlang runtime system documentation)", n.d. Quote: "A native function that crashes will crash the whole VM."

[101] https://go.dev/talks/2015/gogo.slide, "Go in Go (Rob Pike, Gopherfest)", 2015-05-26. Quote: "Measure success by bit-identical output."

[102] https://go.dev/design/go13compiler, "Go 1.3+ Compiler Overhaul (Russ Cox design document)", 2013-12. Quote: "A mechanical translator will instead generate translations with consistent classes of errors, which should be easier to find, and it will not zone out during the tedious parts."

[103] https://go.dev/talks/2014/c2go.slide, "Go, from C to Go (Russ Cox, GopherCon 2014)", 2014-04-25. Quote: "C uses pointers for what Go calls pointers and slices."

[104] https://raw.githubusercontent.com/immunant/c2rust/master/README.md, "C2Rust README", n.d. Quote: "The output of `c2rust transpile` is unsafe and unidiomatic; it is merely the first step in a longer migration process."

[105] https://www.netlib.org/f2c/f2c.pdf, "A Fortran-to-C Converter (AT&T Bell Laboratories CS Technical Report 149)", 1990-05-16. Quote: "As a debugging aid, we sought bit-level compatibility between objects compiled from the C produced by f 2c and objects produced by our local f 77 compiler."

[106] https://www.netlib.org/f2c/f2c.pdf, "A Fortran-to-C Converter (AT&T Bell Laboratories CS Technical Report 149)", 1990-05-16. Quote: "In addition, for every non-function argument that is of type character, an argument giving the length of the value is passed."

[107] https://gitlab.com/x86-psABIs/x86-64-ABI/-/jobs/artifacts/master/raw/x86-64-ABI/abi.pdf?job=build, "System V Application Binary Interface, AMD64 Architecture Processor Supplement (Version 1.0)", 2025-03-12. Quote: "If the class is INTEGER, the next available register of the sequence %rdi, %rsi, %rdx, %rcx, %r8 and %r9 is used"

[108] https://gitlab.com/x86-psABIs/x86-64-ABI/-/jobs/artifacts/master/raw/x86-64-ABI/abi.pdf?job=build, "System V Application Binary Interface, AMD64 Architecture Processor Supplement (Version 1.0)", 2025-03-12. Quote: "If the class is SSE, the next available vector register is used, the registers are taken in the order from %xmm0 to %xmm7."

[109] https://gitlab.com/x86-psABIs/x86-64-ABI/-/jobs/artifacts/master/raw/x86-64-ABI/abi.pdf?job=build, "System V Application Binary Interface, AMD64 Architecture Processor Supplement (Version 1.0)", 2025-03-12. Quote: "The 128-byte area beyond the location pointed to by %rsp is considered to be reserved and shall not be modified by signal or interrupt handlers."

