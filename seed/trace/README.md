# seed/trace: local trace segment (omega-mv #608)

`segment.om` is a library unit in seeded Omega. It has no `main`, so concatenate it in front of a program
that defines one (units concatenate; Omega has no import). Host words: `fopen`, `read`, `write`,
`fseek`, `fsync`, `fclose`, `unlink`, `link`, `renames` (OMEGESE section 10). Linux AArch64 only.

## Format version 1

All integers are little-endian.

| Part | Bytes | Fields |
|------|-------|--------|
| header | 16 | `AITR` (65 73 84 82), version u16 = 1, record size u16 = 64, header size u32 = 16, reserved u32 = 0 |
| record | 64 | type u16, payload length u16 (at most 52), sequence u32, payload 52 bytes (zero past the length), CRC-32 u32 |

- The sequence is the record index, assigned by the writer. A scan refuses a record whose sequence is
  not its position, so a duplicated or reordered record is detected.
- The CRC is CRC-32/IEEE (reflected polynomial 0xEDB88320 = 3988292384, initial value and final xor
  0xFFFFFFFF) over record bytes 0..59. Its check value `CRC-32("123456789") = 3421780262` is the first
  test (Wolfram: `Hash["123456789","CRC32"] -> 3421780262`).
- A segment holds at most 4096 records, so the largest segment is `16 + 64*4096 = 262160` bytes.

## Operations

| Function | Behaviour |
|----------|-----------|
| `segcreate` | creates `tpath` with O_WRONLY, O_CREAT, O_EXCL and O_NOFOLLOW, mode 0600, and writes and fsyncs the header. An existing path is refused (10); it is never truncated |
| `segready` | creates a missing segment. For an existing one it requires a valid header, and it never recreates the file |
| `segappend t n` | appends one record with payload `pay[0..n)`. It refuses if the file length is not header plus whole records (25: torn tail, recover first), when the segment is full (26), or when the payload is over 52 bytes (20). Each record is fsynced |
| `segscan` | read-only. Sets `scount` (valid records before the first damage), `sreason` (0 clean, 1 torn tail, 2 bad length, 3 CRC mismatch, 4 sequence mismatch, 5 over capacity), `stotal` (file bytes) and `slost` (bytes after the valid prefix). A header that is short, has bad magic (11), an unsupported version (15), or bad geometry or nonzero reserved bytes (16) is refused |
| `segrecover` | returns 0 for a clean segment. Otherwise it writes the header and valid prefix to `<segment>.new` (O_EXCL, fsync), hard-links the damaged file to `<segment>.bad` (refused with 32 if that exists, so a quarantine is never overwritten), renames the new file over the segment and returns 1. Header errors are returned, not recovered |

Every open uses O_NOFOLLOW, so a symbolic link as the segment path is refused (ELOOP, `serrno = -40`).
The caller creates directories with mode 0700 (`makedir ... 448`); the test does so.

## Gate

`sh seed/trace/gate.sh`, or `tests/gates/run.sh trace` (manifest `tests/gates/omega-trace.gate`). It
checks `seed/out/omc` against `seed/out/omc.sha256`, compiles `segment.om` plus `segment-test.om`, and
runs the test in `build/seed/trace/main`. The test must exit 0 and its stdout must equal `expected.txt`.
The 71 checks cover:

- appending 100 records;
- truncating the last record to 30 of its 64 bytes; the scan must then keep 99 records and report a
  torn tail;
- append refused until recovery; recovery keeps 99 records and quarantines the 6382-byte file; the next
  append gets sequence 99;
- a flipped payload byte in record 50; the scan must stop at 50 records with a CRC mismatch, and
  recovery is refused while `.bad` exists;
- crafted records with payload length 60 and with sequence 7;
- header version, magic, geometry and reserved-byte refusals;
- a symbolic-link path refused by scan, append and recover.

Four source mutants must each compile, exit 1 and fail their own check: `crc` (CRC no longer compared,
so `crc-reason` fails), `torn` (`torn-reason`), `align` (`torn-append-refused`) and `excl` (create with
O_TRUNC, so `create-again-refused`).

The expected sizes and offsets were computed in Wolfram Language:
`{16+64*100, 16+64*99+30, 16+64*99, 16+64*50+8, (16+64*100)-16-64*50, 16+64*50, Mod[6382-16,64]}
-> {6416, 6382, 6352, 3224, 3200, 3216, 30}`. The flags were checked the same way:
`{BitOr[1,1024,32768], BitOr[1,32768], BitOr[1,64,128,32768], BitOr[1,64,512,32768]} -> {33793, 32769,
32961, 33345}`. The remaining values (errno 2, 17 and 40, and the result codes) are pinned by the source.

## CLI (omega-mv #610)

`tracecli.om` is the offline, read-only inspector. Concatenate it after `segment.om` and `tsha.om`.
Commands: `version`, `verify`, `show`, `events` (`--type`, `--range`), `stats`, `export`, `compare`,
`list`. Nothing is written except stdout. Exit status: 0 ok, 1 a compare found a difference, 2 usage,
3 partial or inconclusive, 4 refused, 5 a stdout write failed. Energy and timing are reported
unavailable: format v1 carries no typed measurements. There is no `steps` command: format v1 has no
step index, and `events` is the typed record listing.

`tracegen.om` writes the fixtures (`fx/` in an empty scratch directory; regular files are left mode
0400). Build it the same way, with `tracegen.om` in place of `tracecli.om`.

`sh seed/trace/cli-gate.sh`, or `tests/gates/run.sh tracecli` (manifest
`tests/gates/omega-tracecli.gate`). It checks `seed/out/omc` against `seed/out/omc.sha256`, compiles
both programs, regenerates the fixtures and checks `fixtures.sha256` (plus the `link.seg` target and
mode 0400), then requires each line of `cli.cases` to match `cli-golden/` and the recorded exit
status. A copy of `fx/a.seg` made mode 000 must be refused (`refused code=10 errno=13`, exit 4) by
`verify`, `show`, `events`, `stats` and `export`. The gate refuses to run as root, because mode 000
would still be readable and the check would pass for the wrong reason.

Four source mutants must each compile and fail their own check:

- `partial`: the unique `ret 3` in `partial` changed to `ret 0`. `verify fx/crc.seg` still prints the
  partial line and exits 0 (need 3). `lentry`'s directory return is spelled `ret ulong 3` so this
  site stays unique.
- `same`: `ret same` changed to `ret true`. `compare fx/a.seg fx/b.seg` prints `differ at=0` (need 17)
  and exits 1.
- `qtype`: the type test inverted (`ne` to `eq`). `events fx/a.seg --type 7` prints `matched=40 of=40`
  (need 0).
- `ropen`: the flags literal `32768` changed to `32770`. WolframLanguageEvaluator: `BitOr[2,32768] ->
  32770` (`O_RDWR|O_NOFOLLOW`). The gate scans `tracecli.om` for the exact return line of `ropen`, and
  this mutant fails that scan. A functional open check does not catch it: `verify` uses `segment.om`'s
  own open, and an `O_RDWR` open of a mode-0400 file is not a reliable refusal for the owner. The
  mode-0000 check is the functional refusal, and it runs on the good CLI.

An edited golden must fail the gate on a stdout mismatch.

## Not in this unit

Issue #608 also lists items this unit does not provide: an index, rotation and retention, an async
writer, schema migration beyond refusing other versions, export, consent and redaction. Concurrent
writers are not supported either: there is no lock, and the unit assumes a single writer.

The CLI above is a separate program. It does not add an index, rotation, retention, an async writer,
consent, redaction, or a query-scaling measurement.

## One-shot SHA-256 digest (security673)

`sha256.om` is a one-shot FIPS 180-4 SHA-256 over at most 4096 input bytes. It holds 32-bit words
in `ulong`, masks every addition, and writes 32 big-endian output bytes. `sha256-test.om` checks the
empty, `abc`, 56-byte, 112-byte and 1000-byte vectors. It is separate from `tsha.om` (the streaming
copy of `om/evidence/evidence.om` that `tracecli.om` uses).

```
seed/out/omc < seed/trace/sha256-gate.om > build/sha256-gate
chmod +x build/sha256-gate
./build/sha256-gate
```

Expected: `sha256 vectors=5 fails=0`, exit 0. The same gate is compiled and run by
`seed/qualification/security673/gate669.om`. The segment writer is qualified by `gate.sh` above,
not by `gate669`.
