# Native host operations

Drake authorized extending Omega before rewriting benchmarks-mv on 2026-10-23,
with no outside implementation help. These operations are implemented in
`seed/omc/omc.om`. That Omega compiler emits the machine instructions itself.
The seed chain builds it and it rebuilds itself to identical bytes. No C/Rust
implementation, translator, external model, foreign compiler, assembler or
linker implements or builds these additions.

The existing shell seed loader remains the first bootstrap step, as described
in `seed/README.md`. Shell scripts coordinate builds and tests; they do not
implement the host operations. Programs call the Linux ARM64 kernel directly,
as the original `read`, `write` and `exit` already do. This is Linux-hosted
operation, not a native AienOS qualification or a GPU qualification.

The language contract is [OMEGESE section 10](../../docs/one_layer/OMEGESE.md).

## Qualification

From the repository root on Linux ARM64:

```sh
quiet-build sh seed/host/check.sh
```

On a machine without the Spark build coordinator, run the script directly.
The script rebuilds the seed chain, proves compiler self-reproduction, runs
native Omega fixtures, rejects malformed inputs, and compares the original
fixture corpus against the original native parent. It does not run the older
C-reference gate scripts. The fixture gate itself is `seed/host/gate.om`,
compiled by `seed/out/omc`; the remaining shell step only builds and launches
it.

Verified on the Spark on 2026-10-23:

- Native file create/read/write/seek/sync/chmod/close/unlink and errno checks.
- Current-directory change, file creation below it and unlink through the
  native `chdir` word.
- Monotonic nanoseconds, realtime nanoseconds and a measured native sleep.
- Argument copying, including empty arguments, spaces, UTF-8 bytes, and refusal
  of short output buffers without modifying them.
- Socket exchange between native Omega processes, including local sockets and
  TCP loopback on a kernel-selected ephemeral port.
- Poll timeouts, fork, accept, exec with exact arguments, exec failure, pipes,
  descriptor duplication, process termination and child reaping.
- Directory creation/enumeration/removal, file metadata and no-replace rename.
- Current-directory and symbolic-link reads with explicit count conventions,
  nonzero slice offsets, truncation checks, overlapping buffers and errno checks.
- Socket options verified by the changed bind result on an ephemeral UDP
  loopback port, including readonly option bytes and invalid options.
- Forty-one negative controls for bounds, malformed packets, readonly arrays,
  wrong types, invalid paths and use outside program mode.
- Seventy original compiler fixtures produce identical machine code, refusal
  status and diagnostics to the original native parent.

Fixtures run in a unique temporary directory under `build/seed/host/`. The
network test connects only to its own local server. Process tests reap their
children. No service is installed or changed. `timeout` only bounds test
processes; timed operations inside the tests use the emitted native code.

### Packed-list hostile ingress (#671)

`seed/qualification/security673/` feeds runtime packets, not compile-time
constants, to `execprog`. `packet-corpus.om` writes one packet per mode byte;
`pkt-consumer.om` reads stdin and passes the bytes, or only the first K bytes
when given one decimal argument, to `execprog` unchanged. `check.sh` runs
`packets.cases` through the OM-2 conductor: 16 rows prove each committed fixture
in `packets/` equals the generator output, 16 rows run the modes (4 valid
packets, including the 64-record and the exact 4096-byte limits, and 12
malformed ones: count 0, 65 and 65535, truncated, over-declared and 65535-byte
records, a trailing byte, an empty first record, zero bytes inside and after the
path, and a 4097-byte packet), and 28 rows truncate the 27-byte echo packet at
every length. Expected exits come from the format rules in OMEGESE section 10;
every malformed packet must trap 3 (exit 103) before any exec.

`packets-mutant.cases` builds three compilers, each without one length check of
the decoder (`hpack` in `seed/omc/omc.om`): the 4096-byte packet bound, the
64-record bound, and exact consumption. Each mutant executes the one packet that
only its check refuses, so `check.sh` requires three exact `FAIL run exit=0
expected=103` verdicts. Removing the record-length-versus-remaining check is
not detected by exit status: the zero-byte and exact-consumption checks still
trap every over-declared record, so that check only bounds how far the decoder
reads. The x86 decoder (`xhpack`) is not exercised here.

### Bounded networking additions (#788)

The source adds `peercred` (keyword 220), `pollfds` (221), and `sendmsg` (222).
`sockopt` now accepts only the scalar transport allowlist in OMEGESE section 10.
The existing `socknew`, `bind`, `listen`, `accept` (`accept4`), `connect`,
`read`/`write`, `recv`/`send`, `waitio`, and `fclose` remain the base transport API.
No host HTTP or JSON implementation is introduced.

`tests/netdata.om` exercises Unix peer PID/UID/GID at a nonzero output offset,
two-descriptor polling and timeout, private single-iovec `sendmsg`, deliberate
short nonblocking sends, closed-peer `EPIPE` without SIGPIPE, descriptor reuse
after repeated close, option allowlisting, and slice/record-size refusals.
`tests/netpeer.om` creates distinct parent/child endpoints, compares peer PIDs
independently, and checks nonzero offsets, untouched surrounding bytes,
negative-timeout readiness and nested expression preservation. It exposed an
older `getppid` lowering error: syscall 110 is `timer_settime` on Linux ARM64,
not `getppid` (173). The old fixture only rejected zero, silently accepting a
negative error. The lowering and positive-PID assertion are corrected here.
Run the candidate host gate without replacing the committed compiler:

```sh
nice -n 19 ionice -c3 sh -c 'seed/out/omc < seed/host/gate.om > build/seed/host/gate && chmod +x build/seed/host/gate && build/seed/host/gate /absolute/path/to/candidate-omc'
OMC=/absolute/path/to/candidate-omc nice -n 19 ionice -c3 sh seed/host/negative.sh
nice -n 19 ionice -c3 python3 seed/host/net-mutant.py
nice -n 19 ionice -c3 sh seed/omc/gate18.sh
```

This is candidate host evidence, not a reseed, compiler fixpoint, release pin,
or hardware qualification. Compiler integration owns the batch reseed.
`net-mutant.py` is a verifier-only Python runner. It compiles each original and
wrong-word source through the committed seed (or `OMC` override), then requires
the real fixture to pass the original and exit 1 for the mutant. Its controls
restore the wrong `getppid` syscall and turn an option refusal into success.
The host gate's compiler argument (`gate.om` argv[1]) applies to every compilation,
including nested device/mapping refusal programs and generated identity fixtures;
early candidate-only logs from before that correction contain mixed-compiler evidence.
The Linux interface authorities are `asm-generic/unistd.h` (`ppoll` 73,
`getsockopt` 209, `sendmsg` 211), `asm-generic/socket.h`, and `linux/tcp.h`.
Instruction emission uses the existing Arm ARM forms; `STR` unsigned-offset
stores private stack records, with `imm12` scaled by eight for 64-bit cells.
Keyword pack verification used WolframLanguageEvaluator:
`Table[{s,FromDigits[Reverse[ToCharacterCode[s]],256]},{s,{"peercred","pollfds","sendmsg"}}]`
returned `{{"peercred",7234314147887146352},{"pollfds",32480013390212976},{"sendmsg",29118836274783603}}`.


## Connected peer metadata

`peername fd A off n` (keyword 224) queries the peer of an existing process
descriptor. Its types, writable slice validation and negative errno behavior
match `sockname`; success returns the full native Linux sockaddr length even
when the supplied slice truncates it. Empty slices are permitted at array end.
Neither word acquires or connects a socket. `accept` keeps its existing API.
Keyword encoding was checked with the mounted WolframLanguageEvaluator:
`FromDigits[Reverse[ToCharacterCode["peername"]],256] -> 7308604897285727600`.

The implementation shares `hbuffer`'s private 32-bit socklen with `sockname`;
checked byte-array capacities fit that representation. Linux source authority:
[v6.12 asm-generic syscall table](https://github.com/torvalds/linux/blob/v6.12/include/uapi/asm-generic/unistd.h)
assigns getpeername 205 and getsockname 204; under `target x86linux` the same
word lowers to x86-64 getpeername 52 (getsockname 51) from
[v6.17 `syscall_64.tbl`](https://github.com/torvalds/linux/blob/v6.17/arch/x86/entry/syscalls/syscall_64.tbl);
[`move_addr_to_user`](https://github.com/torvalds/linux/blob/v6.12/net/socket.c)
copies only the bounded prefix, permits zero capacity, and returns full length.
The [getpeername manual](https://man7.org/linux/man-pages/man2/getpeername.2.html)
defines EBADF, ENOTSOCK and ENOTCONN. Omega preserves raw negative syscall
errno, rather than libc's -1/errno convention.

`tests/peername.om` compares an accepted TCP peer with the independently queried
client endpoint on loopback, checks a distinct local endpoint, offset sentinels,
short/empty slices, closed descriptors, pipes and unconnected sockets.
The host gate also requires bounds traps; negative.sh covers typing, readonly
arrays and function mode. net-mutant.py replaces getpeername with getsockname
and requires the real fixture to reject that compiler.

Transport addresses are not user authentication or an original client identity
behind a proxy. Consumers must validate length and family, retain descriptor
lifetime control, and preserve their existing trusted-proxy boundary. This
compiler change neither activates gateway policy nor trusts forwarding headers.

## Source authority and limits

Instruction forms come from the existing hand-audited instruction table in
`seed/rung2/table.s2`, which identifies Arm manual sections. Linux interfaces
were checked against the installed ARM64 headers: `asm/unistd.h`,
`asm-generic/unistd.h`, `asm/fcntl.h`, `asm-generic/fcntl.h`,
`asm-generic/stat.h`, `asm-generic/poll.h` and `linux/time.h`. The architecture
header overrides generic constants: ARM64 `O_DIRECTORY` is 040000 octal.
These are interface references, not imported implementation code.

The additional keywords 132 through 134 are `cwd`, `readlnk` and `sockopt`.
Installed ARM64 `asm/unistd.h` includes the generic definitions for `getcwd`
(17), `readlinkat` (78) and `setsockopt` (208). `linux/fcntl.h` supplies
`AT_FDCWD`; ARM64 `asm/socket.h` includes the generic `SOL_SOCKET` and
`SO_REUSEADDR` definitions. Adding these reserved words changes the compiler
digest. All 70 tested original fixtures retain identical generated bytes,
refusal status and diagnostics. The three new words can no longer be used as
ordinary identifiers.

This extension supplies host facilities for the benchmark migration. It does
not implement a JSON library, floating-point types, the full benchmark suite,
or the sibling runtime's in-process interfaces. Existing numeric, array,
register and loop capacities remain in force. The language has no exposed
pointers or arbitrary-syscall escape hatch.
