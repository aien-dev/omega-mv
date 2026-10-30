# Omega Fabric (`src/fabric/`)

The Fabric is how separate AIEN machines agree on who is a member, what each one
offers, and how work is handed from one machine to another. Everything here is
plain C with no dynamic allocation, no clocks and no files, so the same code can
run on a coordinator, inside a hosted worker, and later inside AIENOS.

## What is in this folder

| File | Role | Builds into production? |
| --- | --- | --- |
| `fabric.h` / `fabric.c` | F5-0 membership: signed JOIN / RENEW / ADVERTISE / LEAVE, leases, generations, replay refusal | yes |
| `fab_auth_ed25519.h` / `.c` | Ed25519 identity for the Fabric: a machine is its public key, and its machine id is derived from that key | yes |
| `fab_route.h` / `.c` | The check a coordinator runs before sending work: pump the Fabric, revalidate the Skill route, require a live remote owner | yes |
| `fab_work.h` / `.c` | Work hand-off: the PST1 message reader and writer, the HTK1 task and HTR1 receipt codecs, the requester state machine and the executor's durable journal | yes |
| `fab_hmac.h` / `.c` | HMAC stand-in authenticator | test only |
| `fab_loopback.h` / `.c` | In-process loopback transport | test only |
| `fab_dispatch.h` / `.c` | Simulated-machine dispatcher (steps 1-3 are `fab_route.c`; only the procedure table lives here) | test only |

"Test only" files carry an `aien_test_build_*` marker and are refused from any
production binary by `tools/r16_prod_hygiene.sh`.

## Work hand-off (heterogeneous compute, aienix#6 and omega#536)

A **requester** sends a PST1 `request` carrying an HTK1 task. An **executor**
answers with `accepted`, `running` and then exactly one terminal message:
`completed` (with the result digest and an HTR1 receipt), `failed` (with a
reason code) or `cancelled`. Every answer is checked against the task id, the
idempotence key and a rising ordinal; anything out of order, from the wrong side,
or for an unknown task is refused and counted.

Two task kinds are allowlisted today, both with exact recomputable answers:

- **matmul_i64_seeded**: an N by N integer matrix product on inputs generated from
  a seed by splitmix64. The result is the SHA-256 of the big-endian product.
- **tokenize_fixture**: run a pinned tokenizer over a slice of a pinned fixture
  (codec and bounds only in this step; the executor does not run it yet).

The executor journals a task's terminal before it sends it. The same task bytes
asked again replay the journaled answer without re-running; different bytes under
the same key are refused with code 7. A requester that loses the session marks
the task UNCERTAIN and leaves that state only through a signed `resume_snapshot`.

Nothing in this folder opens a socket. Transports are supplied by the caller
(`FabTransport`), so the loopback test, the two-process pipe test and a real
network adapter all exercise the same code. Loopback and pipes are not a network
and are never reported as one.

## Tests

```
make test-fabric          # F5-0 membership, loopback
make test-fabric-work     # PST1 vectors, HTK1/HTR1 goldens, matmul oracle, state machines,
                          # Ed25519 membership, then the same binary under AddressSanitizer,
                          # then requester and executor as two OS processes over pipes
make test-fabric-living   # living-system phase gates
make fabric-purity fab-work-purity   # no forbidden symbols in the production objects
```

`test-fabric-work` needs the pinned AIENOS capability library; point
`AIENOS_LOCK_REPO` at a checkout holding the locked commit if the default
sibling path is absent. The PST1 vectors in `tests/fabric/vectors/pst/` come from
the protocols repository and are checked byte for byte against `expected.txt`.

The matmul oracle digests (seed, size, SHA-256 of the product) are recorded in
`tests/fabric/fab_work_test.c` so an independent implementation can reproduce
them.

## Limits

This folder proves codec, state machine and journal behaviour in one process and
across a process boundary. It does not by itself prove a second physical machine
ran anything: that evidence comes from the hosted worker adapter and the
three-machine qualification in the benchmarks repository.
