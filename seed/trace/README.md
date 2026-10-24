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

## Next

Exact-byte consent and the sanitized export bundle are the remaining #607 work. They need a `.om`
cryptographic digest and the existing Open Humanity receiver contract; neither exists yet, so they
are `NOT_RUN` rather than approximated with a non-cryptographic hash.
