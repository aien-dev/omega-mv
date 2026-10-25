# Native authority repair controls

This supplements #685–#688; it does not qualify production concurrent effects.
`native.json` records the editing-host native run and exact compiler/source/binary
hashes. The original cleanup compile error is retained as INVALID, not a mutant.
The corrected cleanup fixture checks the identical result, trace and descriptor
assertions through a register-bounded helper.

Use an otherwise clean disposable Linux AArch64 checkout of this branch. Rebuild
with the existing `sh seed/omega-from-seed.sh`; require the compiler SHA-256
`66b4cf34e5cd2c8ff003bd142e7309f52178b66c2380f78ac313b6eb8b67471b`.
Never repin a consumer to this old compiler: omnet's self-exec descriptor protocol
requires its own newer compiler.

For each row, compile the gate using `seed/out/omc`, require compiler exit 0,
mark the resulting executable, and run it in a separate empty working directory.
These shell commands are interactive reproduction instructions, not a new runner:

```sh
mkdir -p build/repair-replay/order
seed/out/omc < seed/authority/dispatch-order-gate.om > build/repair-replay/order-gate
chmod +x build/repair-replay/order-gate
(cd build/repair-replay/order && ../order-gate)
```

| Gate | Positive assertion | Mutation mode | Negative assertion |
|---|---|---|---|
| dispatch-order | 3 checks, fails=0 | 0 | acquired-record refusal (also read/acquisition error cleanup) |
| store-exact | 4 checks, fails=0 | 1 | trailing-record refusal |
| lock | 8 checks, fails=0 | 2 | missing/directory unlink error accumulated by the gate |
| store-cleanup | 18 cases, fails=0 | 3 | `cbadcase`: result, ordered effects and descriptor identity |

Build `mutate.om` with the same compiler. It reads a gate from stdin, writes its
mutant to stdout, and takes the table's mode as its sole argument. It requires
one exact source match (four in mode 3), refusing an unexpected source with 93
before emitting any bytes. Argument/input/output errors are 90/91/92/94.

```sh
seed/out/omc < seed/qualification/security673/repairs/mutate.om > build/repair-replay/mutate
chmod +x build/repair-replay/mutate
build/repair-replay/mutate 0 < seed/authority/dispatch-order-gate.om > build/repair-replay/order-mutant.om
seed/out/omc < build/repair-replay/order-mutant.om > build/repair-replay/order-mutant
chmod +x build/repair-replay/order-mutant
mkdir -p build/repair-replay/order-negative
(cd build/repair-replay/order-negative && ../order-mutant)
```

Require each preceding command to succeed before executing its output. Each
negative executable must finish normally with exit 1 from its intended assertion.
A signal, timeout, malformed executable or compiler failure is INVALID. Lock and
cleanup failures have no stdout; inspect their explicit aggregate-assertion
return sites, not merely any nonzero exit. Store-exact mode 1 deliberately does
not remove the generation probe, so it proves only record-probe sensitivity.

Dispatch-order uses an acquisition schedule double; cleanup uses handwritten
write-side effect doubles. Exact-input and lock gates exercise real filesystem
operations in disposable directories. None qualifies arbitrary filesystem
interference, production revocation fencing, stale-pid ABA safety, power loss or
aggregate durable accounting. Replay the full existing coordinator separately;
its status is not inferred from these four fixtures.
