# OSH cross-adapter pipeline differential (omega#568)

Status: host side runs; the AIENOS native column reports NOT_RUN until native multi-command execution exists
(aienos-mv#429/#430). Parent: aien-architecture-mv#158. Contract: aien-protocols-mv `specs/osh-platform/OSH_PLATFORM_ABI.md`
v1 DRAFT at `fb5fca98e6676bb104888c73268e0f9a81e9c63e` (no `feat/osh-stream-abi-43` branch existed when this was written).

`make test-osh-diff` builds and runs:

- `osh_diff` (plain, then ASan/UBSan): every fixture in `fixtures.txt` through the real compiled Omega shell core (the three
  `.osc` units embedded in `build/osh/osh_units.c`, driven by `osh_shell_run_string`, the same loop as the `osh` program) on
  the Linux host adapter, once with native AArch64 units and once with the OSC interpreter;
- `osh_toy`, the toy stage operations, linked under each name into a private PATH directory;
- `mutants.sh`, which proves the runner goes red;
- a symbol check that the TEST-ONLY observer (`-DOSH_DIFF_TEST_HOOKS`, `OshDiffHooks` in `src/osh/host/osh_shell.h`) is
  absent from the shipped `build/osh/osh`.

Options: `OSH_DIFF_NATIVE=FILE` compares a native result file (a raw QEMU serial log is fine);
`OSH_DIFF_REQUIRE_NATIVE=1` makes any portable NOT_RUN a failure (acceptance mode).

## Columns and verdicts

| column | PASS means | NOT_RUN means |
|---|---|---|
| host-native | the run matched every expectation of the fixture, used only the native core backend, left no descriptor open and no process in the pipeline group (ABI section 12) | the fixture cannot apply in this build (the RLIMIT_AS fixture under ASan; RLIMIT_NPROC when run as root) |
| host-interp | same, interpreter backend, and the normalized result line equals host-native's | same |
| aienos | the native line for the fixture equals the host-native line, field by field | no native file, no line for that fixture, or the fixture is `scope linux` |

A supplied native file that is invalid fails every portable fixture. Invalid means: no `OSHDIFF-RESULT 1` header, no pin
line, a missing pin, no end line or a wrong count (a truncated log), a duplicate or unknown fixture, any unknown OSHDIFF line,
`adapter` not `aienos-native`, or `abi`, `fixtures` or `core` different from the host run. NOT_RUN is never PASS: the
final line is `OSH_DIFF_PASS` only when nothing failed, and with `OSH_DIFF_REQUIRE_NATIVE=1` only when every portable
fixture has a native PASS.

## Fixture format (`fixtures.txt`)

Line oriented; `#` comments; one `abi` pin line; fixtures from `fixture ID` to `end`. Strings are double quoted with the
vectors/README escapes `\"`, `\\`, `\xHH`, plus `\n` and `\t` for readability.

| key | meaning |
|---|---|
| `scope portable` / `scope linux` | portable: every adapter must agree. linux: the mechanism is Linux-only (rlimits, stop signals, EMFILE); native NOT_RUN by design |
| `covers TEXT` | what the fixture proves |
| `text "..."` | shell input bytes, handed to the core as one `-c` string |
| `direct "a","b" \| "c"` | AIEN direct-argv submission (ABI section 11), one pipeline; no shell text, the core is not called |
| `stdin "..."`, `file NAME "..."` | standard input; files created in the run directory first |
| `fault ...` | injected fault (below) |
| `limit ...` | resource limit (below) |
| `req LINE` | every executed request record, in the vectors/README grammar, pipelines `P0`, `P1`, ... in execution order (a skipped `&&`/`\|\|` pipeline produces no record) |
| `refuse CODE NAME off=N` | the core refused a list (ABI 5.3, 5.4) |
| `stdout "..."`, `status N` | exact standard output bytes and final shell status |
| `stages P0=a,b P1=c`, `err P0=NAME` | stage categories and pipeline platform error (ABI 8.6) per pipeline |
| `outfile NAME "..."`, `outlen NAME N` | file content, or only its length, after the run |
| `stderr_has "..."` | host-only: a diagnostic substring (diagnostic text is not compared across adapters) |

Grammar extension (UNVERIFIED, the vectors grammar has no direct form): ` direct` follows `conn=N` when request flags bit 0
is set.

### Faults (portable meaning; each adapter injects them its own way)

| fault | meaning | Linux host injection |
|---|---|---|
| `launch_fail_at=N` | launching stage N fails | `OshSession.fail_fork_at` |
| `revoke_spawn=NAME` | the capability for spawning NAME is revoked before its effect | effect hook returns `REVOKED` |
| `interrupt_after_launch` | cancel in interrupt mode once every stage is launched | `kill(0, SIGINT)` from `after_launch_hook` |
| `stop_first_after_launch` | Linux only: one member receives SIGSTOP | SIGSTOP to the first child listed in `/proc/self/task/*/children` |

`mutant_leak_fd` and `mutant_leak_process` exist only for `mutants.sh`; they break cleanup on purpose.

### Limits (Linux only)

`nofile=tight` (RLIMIT_NOFILE leaves exactly one free descriptor), `nproc=N`, `fsize=N` and `as=BYTES`, applied in the run
child just before the shell runs, so the shell and every stage inherit them.

## Toy operations

The contract both adapters implement. On Linux they are `osh_toy.c`; on AIENOS they are Omega-native operations
(resolution class 1) with the same names and the same bytes.

| name | behaviour |
|---|---|
| `emit ARGS...` | each argument plus a newline |
| `args ARGS...` | `N\n`, then `LEN:BYTES\n` per argument |
| `pass` | copy input to output, reads of up to 4096 bytes |
| `trickle` | copy one byte per read and one byte per write |
| `flood N` | N bytes, byte i = `'a' + i % 26` |
| `bytes` | the 256 byte values 0 to 255 once |
| `take N` | read until N bytes or end of input, write them, end without reading more |
| `sink` | read to end of input, print `n=COUNT sum=FNV1A32\n` (8 lower-case hex digits) |
| `status N` | end with status N, no output |
| `oe` | `O` on output, then `E` on error output |
| `stall` | wait until cancelled |

## Stage categories (the receipt categories compared across adapters)

Each command's ABI section 10 outcome plus how it ended. UNVERIFIED: the signal mappings are this harness's choice.

| category | Linux host source |
|---|---|
| `ok`, `exit:N` | COMPLETED, no signal, status 0 or N |
| `broken_pipe` | COMPLETED, killed by SIGPIPE (native: a write returned `BROKEN_PIPE`) |
| `interrupted` | COMPLETED, killed by SIGINT |
| `limit` | COMPLETED, killed by SIGXFSZ |
| `signal:N` | COMPLETED, any other signal (Linux only) |
| `cancelled`, `not_started`, `unknown` | CANCELLED, NOT_STARTED, OUTCOME_UNKNOWN |
| `not_found` | FAILED_NO_EFFECT, `NOT_FOUND` |
| `refused:NAME` | FAILED_NO_EFFECT, `DENIED`, `REVOKED`, `STALE`, `CAP_DOMAIN_MISMATCH` or `CAP_GEN_NARROW` |
| `failed:NAME` | FAILED_NO_EFFECT, any other platform error |

## Result file (what the AIENOS native harness must print)

The host writes the same format with `--emit` (`build/osh/diff-host.result`, adapter `linux-host`); copy it for a worked
example. Only lines that start with `OSHDIFF` count, after removing a trailing CR, so the native harness can print them on
the serial console among other output and the QEMU log is passed as is.

```
OSHDIFF-RESULT 1
OSHDIFF pin adapter=aienos-native aienos=<sha> omega=<sha> core=<sha256> abi=<pin> cc=<toolchain> harness=<sha> fixtures=<sha256>
OSHDIFF vec <fixture id> req=<sha256>[,<sha256>...]|none refuse=<code>:<off>|none status=<n> stages=P0=<cat>,<cat>[;P1=...]|none err=P0=<NAME>[;P1=...]|none out=<len>:<sha256> files=<name>:<len>:<sha256>[,...]|none
OSHDIFF end n=<number of vec lines>
```

- `req`: sha256 of each executed request record in ABI 7.3 wire form: 16-byte header `{magic u32 0x4F534851, abi_version
  u16 1, kind u16 1, total_len u32, reserved u32 0}`, the record cells (8 header + 164 per command) as u64 little-endian,
  then `out_used` bytes of OUT, one byte per OUT cell (UNVERIFIED: the draft says "out_used bytes of OUT" without saying
  one byte per cell). The same core gives the same bytes on every adapter; any difference fails.
- `out`: length and sha256 of standard output. `files`: for each `outfile`/`outlen` key of the fixture, in fixture order.
- `core`: sha256 over, for lex, parse, expand in that order, the source length as u64 little-endian then the `.osc`
  source bytes. `fixtures`: sha256 of `fixtures.txt`. Both must equal the host's.
- One line per fixture the native harness actually ran. A fixture it cannot run (a fault it cannot inject) is left out and
  shows NOT_RUN.

## Pins

The host pin line records `omega` (git HEAD, `-dirty` if `src`, `tests` or `mk` differ), `core`, `abi`, `cc` (compiler
name, version, first 16 hex digits of the sha256 of the compiler driver), `harness` (sha256 of `osh_diff.c`, `osh_toy.c`
and `mutants.sh`), `fixtures`, and `aienos=lock:<sha>` (the AIENOS commit `aienos.lock` pins; the host runs no AIENOS).

## What this does not prove

Host and interpreter results only. No QEMU or hardware run exists: the AIENOS column is NOT_RUN. Revocation is checked at
the spawn effect boundary only; revocation during a blocked native read or write needs the native stream service. The
durable journal is not enabled in these runs (`test-osh-journal` covers it).

## Result-reader boundary follow-up (L553)

Each identity key occurs exactly once. Tokens lacking `=`, empty/oversized
values and unknown keys make the entire supplied result invalid; repeated keys
are invalid even when their values agree. Metadata records provenance and does
not authenticate execution. Synthetic native-looking files are comparator tests
only, never AIENOS qualification.

Run `node tests/osh/diff/result_reader_controls.mjs RUNNER TOY FIXTURES FRESH_OUT pins`
with built absolute paths (or relative paths from the repository root). The first
leg executes all original host fixtures, then probes the actual reader through
its CLI. An optional final host-result path runs comparator-only controls, used
for sanitizer and mutation legs. Compile-time behavioral controls are in
`result_reader_mutants.mjs`; build errors, signals and deadlines never count as kills.

Imported vector strings are released on successful comparison and invalid-host
refusal. The sanitizer comparator leg enables leak detection normally. It does
not turn off a sanitizer or count a diagnostic as an expected refusal.
