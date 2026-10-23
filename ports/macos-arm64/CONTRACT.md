# L615 target contract, revision 1

This is a bounded source-built format/target adapter, not a source migration or
complete macOS toolchain. Production logic is handwritten `adapter.om`, compiled
by the immutable seed-born compiler. Input is an explicitly validated subset of
that compiler's Linux ELF output. It emits an unsigned dynamic ARM64 Mach-O.
The shell drivers handle files; Node and LLVM are external qualification oracles.
No C, Rust, Python, LLVM or Apple compiler constructs the production output.

## Pins and ownership

Baseline source `57f5f4258cebf3d5b2c22853cd062e351265c525`; shared compiler
SHA256 `191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`,
source ancestry `29b12760`, reviewed payload
`70459b34b8c4840c48c2b15f2edc4b2a7c39ea75`. The compiler hash is checked
immediately before every invocation, including compiled negative controls.
L408 owns compiler source; L615 never builds or edits it. L613 owns native Mac
execution, independent review and final integration. Issue634 remains OPEN.

## Input closure

Read raw stdin to EOF, at most 16384 bytes. Fixed 16385-byte input storage detects
one excess byte. Minimum 212 bytes, multiple of four. Exact ELF64 little-endian
AArch64 ET_EXEC header: entry 0x4000b0, program-header offset64/count2/size56,
no section table or relocations. RX segment starts at file0/VM0x400000,
file and memory sizes equal exact input length. RW segment at0x10000000,
file size0/memory size8388608, alignment4096. All other bytes of the 176-byte
header must equal the compiler's zero-filled template. Globals change memory
size and are refused. Extended, stripped, linked, PIE, foreign, old or partially
recognized ELF layouts are refused.

The branch at176 must lead exactly to the last24 bytes, a fixed bootstrap:
MOVZ X28,#0x1000,LSL16; MOVZ X16,#0x1080,LSL16; MOV SP,X16; BL main;
MOVZ X8,#93; SVC0. The BL must lead to180 (leaf family), or196 (exact hello).
Void-main bootstraps and additional source functions are outside the closure.

Leaf family: every 32-bit body instruction from180 to bootstrap is validated.
Integer register operands are X0..X17 or XZR, with destination restrictions in
the decoder; no X18, X19..X29, SP or LR access. Only exact RET X30 is allowed.
The following instruction forms, as expressed by the masks in adapter.om, are
accepted. Semantics are the corresponding AArch64 instructions, including W
register zero extension, NZCV updates, modular unsigned arithmetic and division.
The adapter preserves bytes and relative distances for every unchanged word.

- 64-bit MOVN/MOVZ/MOVK, four halfword positions, destination X0..X17.
- 32/64-bit ADD/ADDS/SUB/SUBS shifted-register form with shift zero.
- 64-bit ADD/ADDS/SUB/SUBS immediate, imm12, shift zero; SP source forbidden;
  destination31 allowed only for flag-setting comparisons (XZR).
- 64-bit SBFM/UBFM, CSET conditions0..13, AND/ORR/EOR unshifted registers.
- 64-bit UDIV/SDIV, LSLV/LSRV/ASRV, MUL with fixed Ra=XZR; 32-bit MUL.
- B, B.cond conditions0..13, 64-bit CBZ/CBNZ. Signed targets must stay in the
  body on a word boundary, and cannot point at the SVC half of an exit pair.
- Exact RET X30 and exact MOVZ X8,#93; SVC0 pairs (normal/trap exit).
  No orphaned syscall or syscall-number instruction is accepted.

The final body word must be RET, SVC of a validated exit pair, or B. Conditional
fallthrough into bootstrap is refused. No calls, memory access, globals, local
arrays, structs, text addresses, ADR/ADRP, indirect branches, pointer operations,
sign-extension checks, FP/SIMD, atomics, PAC, system registers, exceptions or
unknown encodings are accepted in the leaf family. This intentionally refuses
some otherwise valid integer source constructs. Loop termination and operation
counts retain the source program's semantics. There is no proof of termination
for adversarial hand-authored machine-code inputs. Replay harnesses impose a
separate child timeout; this is not a sandbox for hostile code.

Hello family: exactly376 bytes, with every byte from176..375 matching the
handwritten word specification in `hello`. The full header is also validated.
This is precisely the pinned compiler's result for tests/hello.om: the16 bytes
`hello omega mac\n`, one write to stdout, return0 iff16 bytes were written,
otherwise74. Payload bytes, FD, length, addresses, stack save/restore, bounds,
error branch, trap tails, entry and return instructions have no wildcard.
Other strings and every stdin program are refused. No compiler keyword was
added or inferred. Input provenance alone is not authentication: any bytes
matching the same frozen language are accepted regardless of their producer.

## Complete target transformations and runtime ABI

The input payload [176,length) is copied to Mach-O file offset16384. All original
PC-relative branches retain distances. Bootstrap uses X28=0, NOP, NOP in place
of Linux stack relocation, preserving the OS-provided aligned stack. The leaf
family does not access memory. Exact hello uses48 bytes of stack, never recurses,
saves/restores X30, and leaves its original stack pointer restored. X18 is never
used. Programs terminate through a syscall, so no foreign caller receives
modified callee-saved state. This internal leaf calling convention is not a
public Apple C ABI and does not accept arguments, argc/argv or runtime callbacks.

Every validated Linux exit pair becomes MOVZ X16,#1; SVC #0x80. X0 holds the
status; the observable process exit status is its low8 bits. Trap tails retain
100+code. On the exact hello, the two-word absolute text address becomes
ADR X1,#-84; NOP, independent of ASLR slide. Its Linux write pair becomes BL
appended-thunk; NOP. The four-word thunk is MOVZ X16,#4; SVC80;
CNEG X0,X0,CS; RET. Darwin carry-set errors become negative results. The caller
only distinguishes16 from all other results, yielding0 or74. Short writes are
not retried; no errno numeric equality between Linux and Darwin is claimed.
Darwin may clear X1 on syscall return; exact hello's subsequent test only uses
X0, and its saved arguments and LR are restored as emitted by the compiler.
SIGPIPE behavior is inherited from the OS; the receipt tests closed stdout
(EBADF), not a closed pipe. Stdin, errno exposure, sockets and other host calls
are outside this milestone.

Output is exactly32784 bytes, little-endian ARM64_ALL MH_EXECUTE,
MH_NOUNDEFS|MH_DYLDLINK|MH_TWOLEVEL|MH_PIE. Eleven load commands totaling608
bytes follow the32-byte header: PAGEZERO (4GB), RX TEXT (32KB, one text section),
read-only LINKEDIT (16KB VM,16-byte file), empty DYLD_INFO_ONLY, SYMTAB (empty
symbols/16-byte empty strings), DYSYMTAB (empty), LOAD_DYLINKER /usr/lib/dyld,
LOAD_DYLIB /usr/lib/libSystem.B.dylib, BUILD_VERSION macOS11.0, fixed format UUID,
MAIN pointing at the adapted bootstrap. This declares an intended minimum;
macOS11 and later versions are NOT_RUN until actual hardware receipts exist.
No bind, rebase, external symbols, TLS, unwinding, initializers or fixups exist.
The optional16-byte write thunk follows the copied code; otherwise zero padding
occupies that reserved space. All other output bytes start at zero.

Dyld and libSystem are required native host facilities. No program computation
is delegated to libc. Apple production kernels refuse static ARM64 executables
and require a4GB inaccessible page zero. Apple ARM64 code signing remains a
separate, explicitly disclosed host dependency: parent copies unsigned output
and uses `/usr/bin/codesign --force --sign -`, then strict signature verification.
No computation-independence claim includes signing or Apple loader behavior.
No native macOS PASS follows from an ELF run, LLVM parsing, disassembly or any
emulator. No emulator is used in this gate.

## Refusal precedence and output preservation

First error wins, in this order:

| Exit | Meaning |
| --- | --- |
|20|stdin read error|
|21|oversized, too short, or partial word|
|22|wrong ELF magic|
|23|wrong ELF machine ISA|
|24|any other header/segment discrepancy|
|25|bootstrap, entry branch or main target mismatch|
|28|hello-family length/complete byte-template mismatch|
|26|leaf terminal shape, unknown instruction/register or malformed syscall pair|
|27|otherwise recognized branch outside body or into exit SVC|
|29|output write failure after successful validation|

Leaf terminal shape is checked before the per-word scan, which is increasing
file offset. Branch target validation occurs at each recognized branch. These
ordering rules distinguish malformed inputs with multiple defects.
No byte is written to stdout before complete validation. The native adapter has
fixed global input/output pools, no heap, bounded input scan and finite validation
loops. `target.sh` writes to a temporary sibling and renames only on success,
preserving an existing output's bytes and mode on refusal. Direct shell redirection
to a final filename would truncate it before the adapter starts, so consumers
requiring preservation must use target.sh or equivalent atomic host glue.
File I/O, timeout, chmod, rename, build orchestration and checks remain disclosed
shell/Node host glue, not seeded host-facility retirement.

Host wrapper output paths must not name directories (including symlinks to
directories). Both build.sh and target.sh refuse these paths with status73,
preserving the directory and its contents. Publication uses GNU mv -T on the
Linux construction host so a directory appearing after preflight cannot be
silently treated as a container destination. This is not a hostile-race sandbox.
