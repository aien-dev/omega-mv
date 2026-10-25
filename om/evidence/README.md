# om/evidence: seeded Omega port of omega_vector.c and omega_evidence.c

`evidence.om` ports the computing parts of `src/omega_vector.c` (113 lines) and `src/omega_evidence.c`
(195 lines) to seeded Omega (aien-dev/omega-mv#726, aien-dev/aien-architecture-mv#300/#316). The part
above the line `note ==== parity harness` is the library: 13 functions with array parameters and integer
results, bindable through the omega-bridge native call ABI v1. The part below it is a program-mode parity
harness. The harness also performs the C's process and file effects, because Omega allows host words
only in program mode.

Reproduce from a fresh clone:

```
OMEGA_OMC=/path/to/seed/out/omc [OMEGA_BRIDGE=/path/to/omega-bridge@012860db] sh om/evidence/parity/run.sh
```

`run.sh` compares the .om output with C oracle hashes recorded in `parity/expected.txt`. The C oracle
driver is not in the tree. It takes about 6 s on the Spark.

## What the C files contain, and what is ported

The task spec described "vector construction/hashing" over vectors of 0..4096 elements and "evidence
record formatting" with random fields. The C does not match that description. This port follows the C:

| C function | port | notes |
|---|---|---|
| `omega_vector_spec_init` | `vecspec` | name `strncpy` 63, SHA-256 of `"OMEGA_VECADD_U32"` ‖ width u16 LE ‖ count u32 LE ‖ overflow u8; the four fixed fields come back in `fld` |
| `omega_vector_oracle_u32` | `vecadd` | |
| `omega_vector_verify_oracle` | `vecver` | `idx` is written only on a mismatch |
| `omega_vector_generate_deterministic` | `vecgen` | |
| `omega_vector_build_spec_graph` | **not ported** | builds an OmegaGraph through `omega_core.c`/`omega_canonical.c` (`omega_build_*`, `omega_compute_semantic_id`). It is a port of the graph core, not of this file; it has 1 caller (`callsites.txt`) |
| `trim_line` + `popen_read_line` length rule, `omega_evidence_physics_commit` | `lnacc` | operates on the 256-byte buffer `fgets` filled |
| `omega_evidence_run_commit` | `cmtacc` | |
| `omega_evidence_run_id` | `runid` | `gmtime_r` (civil-from-days) + `strftime("%Y%m%dT%H%M%SZ")` + `"-"` + sha or `"unknown"` |
| `omega_evidence_path` | `evpath` | the string and both length limits; `mkdir_p_for_file` stays with the caller (harness: `mkdirp`) |
| `omega_evidence_write_digest` | `digrel` + `shainit/shaupd/shafin` | the content name; the `O_EXCL` 0444 create and write loop stay with the caller (harness: `excl`) |
| `omega_evidence_hardware_from_nvrm` | `hwhex` | `compute_class`/`sm_version` are copies and `alias` is the literal `"sm_121"`, so both stay with the caller |
| `omega_evidence_tree_dirty` | **caller** | "any line from popen, or popen failed" has no computation to port; the harness reproduces it |
| `omega_evidence_self_binary` | **not ported** | readlink(`/proc/self/exe`) + `getenv("OUT_DIR")` + `access`, which are effects only. It is not in the corpus: its result depends on the running binary |

`omega_evidence.h` also declares `omega_run_m4_gates` .. `omega_get_m19_gate_snapshot`. These are
defined in `tools/omegatool.c`, not in `omega_evidence.c`, and are out of scope.

SHA-256 is adapted from `tools/zero-legacy/sha256.om` (arch-316-closure), with the state moved into
parameters. There is no `import`, so the code is copied. `om/sha256/sha256.om` (bound as `omega_omsha`,
already used by `src/sha256.c`) is the other existing Omega SHA-256. A cutover can call it instead of
`shainit/shaupd/shafin`.

## Oracle

The C at omega-mv `acb3f15cd`: `omega_vector.c` `77057d59…7ed1`, `omega_vector.h` `b50a9218…9f43`,
`omega_evidence.c` `7d533625…fa94`, `omega_evidence.h` `57b3e231…94c0`, `sha256.c` `ea8ae1c8…1ed15` (which
itself calls `om/sha256` through `src/omega_om_units.inc`). `nvrm.h` comes from physics `048e5f5e`
(physics.lock). Compiler: gcc 13.3.0 `-std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -O2`, the Makefile
CFLAGS, aarch64. `.text` sha256: omega_vector `124ee068…d23d`, omega_evidence `dc7725ea…adb`. The objects
are unchanged. The driver interposes `popen`/`pclose`/`time` at link time: `popen` returns the record's
bytes through `fmemopen`, so `fgets` is the real libc one. `run_id` runs in a forked child per record,
because it is cached in a static. `physics_commit` reads a real `physics.lock` file. `path` and
`write_digest` run for real in an empty directory, and the tree they leave is compared as well (names,
types, modes, sizes and file sha256). In program mode the harness does the same effects with the same
flags (`O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC`, mode 0444, directories 0755, umask 022).

omc sha256 `734f0a39…6b61`. `evidence.om` compiles to a 35,032-byte program.

| stream | function(s) | records | result |
|---|---|---|---|
| vspec | `vecspec` | 300: NULL name, n 0, n 1/2/4096/2^31/2^32-1, NUL inside the name, 60..67-byte names around the 63 cut, random 0..100-byte names | 300/300 |
| vadd | `vecadd` `vecver` | 500 vectors, n 0..4096 (0, 1, 2, 4095, 4096), wrap-heavy inputs, exact, 1-bit flip, all poison, last element, two mismatches | 500/500 (c bytes, rc, first index) |
| vgen | `vecgen` | 40 lengths 0..4096 | 40/40 (a, b, c) |
| line | `cmtacc` `lnacc` | 324: run_commit with popen ok/NULL, physics.lock present/absent (n 0..300, trimmed length n-2..n), tree_dirty; empty, `\n` only, CR/space trims, 39/41 chars, NUL inside, >255 bytes, two lines | 324/324 |
| runid | `runid` | 200: years 1 and 9999, the epoch, -1, 2^31 edges, random; good, bad and failing sha lines | 200/200 |
| hw | `hwhex` | 100: NULL rm, zero and random uuids | 100/100 |
| path | `evpath` | 300: NULL rel, n 0, path length 1022..1025 around the 1024 limit, n = len-1/len/len+1, random | 300/300 + tree (7,676 lines) |
| digest | `digrel` `sha*` | 200: NULL stem/ext/buf (len 0 accepted, len 5 refused), rel length around 768, buffer 0..20000 with 55/56/63/64/4096/4097/8192 edges, exact repeats (rc 1) | 200/200 + tree (1,880 lines) |

Sixteen small mutants are part of the verdict. Each must change its stream's hash. All are killed, for
example the overflow byte in the spec digest (280 lines), a 64-byte name cut (98), `trim_line` without
CR (14), `>=` → `>` in the length rule (8), non-floor division for negative times (42), the leap month
(14), uppercase hex (94), the 1024 limit (300), directory mode (pathtree), the EEXIST return (12), SHA
padding at 55 bytes (17) and file mode (digtree). See `parity/run.sh`.

## Limits

- Fixed ABI arrays: vectors are at most 4096 uint32 (16384 LE bytes). A larger `n` traps 3 where the C
  would loop over the caller's arrays. A name enters as its first 64 bytes, a `relpath` as 1024 bytes,
  stem and ext as 768 bytes each, the fgets buffer as 256 bytes, the run id as 96 bytes. Longer C
  strings give -1 in the C anyway, so the adapter passes the length and the port refuses before it
  reads past the array.
- NULL pointers cannot be expressed. `vecspec` takes `has` for a NULL name. In `evpath` and `digrel`,
  the NULL `relpath`, `stem`, `ext`, `out` and `buf`-with-`len`, and `n == 0`, are the adapter's
  checks, as the harness does them.
- `vecspec` `n` above 2^32-1 traps 5 (the C parameter is uint32). `runid` `secs` outside years 1..9999
  traps 4 (glibc `%Y` beyond that is not modelled). A 256-byte fgets buffer without a NUL traps 4 (fgets
  always writes one).
- `evpath` returns -2 when the path is built but does not fit `n`. The C still creates the directories
  in that case, so the adapter must call `mkdir_p` before refusing.
- `shaupd` takes multiples of 64 bytes (trap 4 otherwise). Callers pass 4096-byte pieces and give the
  rest to `shafin`.
- Not ported: `omega_vector_build_spec_graph`, `omega_evidence_self_binary`, and the effect halves
  (popen, fopen, mkdir, open/write/unlink, the static run-id cache). No C file or caller changes here.
  The C adapters, and removing the two objects through omega-bridge #8, are future work. A cutover that
  keeps the C entry-point names as adapters needs no caller changes. `callsites.txt` lists the 150
  callers (file:line) by function.

## NOT_RUN

- No GPU, model or serving test: nothing here runs on the device.
- No `make test`. The C files and every C caller are unchanged.
