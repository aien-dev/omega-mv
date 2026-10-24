# Local trace segment in seeded `.om`

`segment.om` is the local, offline trace writer replacement for the legacy trace owners. It writes
one file with a 16-byte header and fixed 64-byte records:

```
0..3   "AITR"
4..5   version 1 (u16 little-endian)
6..7   record size 64 (u16)
8..11  header size 16 (u32)
12..15 reserved zero

record: type u16, payload length u16 (<=56), sequence u32, 56 payload bytes
```

Append uses `O_WRONLY|O_CREAT|O_APPEND` and `fsync`. A missing or invalid header recreates the
segment. `segcount` scans complete records and reports a short tail; `segrecover` renames the
damaged segment to `segment.bad` and creates a fresh one. No network, background upload, consent,
export or retention policy is implemented here.

## Run

```
seed/out/omc < seed/trace/segment-gate.om > build/segment-gate
chmod +x build/segment-gate
./build/segment-gate
```

Expected: `trace checks=117 000`, exit 0. The same program runs inside `gate669`.

## SHA-256 digest

`sha256.om` is a one-shot FIPS 180-4 SHA-256 over at most 4096 input bytes. It holds 32-bit words
in `ulong`, masks every addition, and writes 32 big-endian output bytes. `sha256-test.om` checks the
empty, `abc`, 56-byte, 112-byte and 1000-byte vectors.

```
seed/out/omc < seed/trace/sha256-gate.om > build/sha256-gate
chmod +x build/sha256-gate
./build/sha256-gate
```

Expected: `sha256 vectors=5 fails=0`, exit 0.

## Next

The digest prerequisite is present. Exact-byte consent and the sanitized export bundle remain
`NOT_RUN`: the Open Humanity receiver contract is not in this repository, and this branch does not
invent a second wire contract. The sender-side bundle waits on that owner.
