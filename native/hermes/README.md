# Omega-native Hermes gateway foundation

**Work in progress. This is not yet a usable Allen gateway.**

All newly authored executable code and test programs in this module are Omega.
The code compiles directly with the pinned seed-born Omega compiler to a static
Linux AArch64 executable. It does not translate through C, link Rust, or load a
Python/Node runtime. POSIX commands used to assemble source files, invoke the
compiler and compare test output are disclosed development orchestration, not
part of the executable. Existing Hermes source is a wire-contract reference,
not a copied backend.

## Implemented and executed

- `src/json.om`: strict bounded JSON parser with token spans and parents. Checks
  numbers, escapes, surrogate pairs, UTF-8, trailing bytes, depth and capacity.
- `src/rpc.om`: envelope validation, decoded top-level key matching, duplicate
  field refusal, exact ID echo, notification suppression, Hermes `ping` result
  `{"pong":true}`, explicit errors for unsupported methods and bad parameters.
- `src/stream.om`: byte-fed newline framing, oversized-line discard and clean
  recovery, CRLF input, final unterminated frame at EOF.
- `src/stdio.om`: Linux AArch64 stdio qualification adapter, partial-write and
  interrupted-call handling. This is a bounded probe, not a permanent daemon.

Native tests: JSON 34/34, RPC 20/20, stream 18/18. All tests were first executed
against compiling stubs and failed on missing behavior. Original RED logs are
retained. The initial JSON span assertion was changed from an unchecked numeric
offset to checking the actual referenced opening-brace byte before implementation.
The RPC implementation initially failed compilation at a nested expression with
error12; reducing expression register pressure and moving token skipping into
its own function corrected it. This was not counted as a behavioral failure.

Two compiled negative controls: removing duplicate-field rejection fails RPC
checks09/10 (exit2), accepting overlong UTF-8 fails JSON check27 (exit1).
The stdio exchange first failed a byte comparison against the stub, then matched
`tests/stdio.expected` exactly. The synthetic `session.resume` is **refused**,
not fabricated. Syscall trace shows only execve, read, write and exit for that
exchange; ELF has no dynamic interpreter segment.

## Bounds and restrictions

65,536 input bytes, 4,096 tokens, 32 container levels. One active stream per
state instance; caller consumes `rout` before feeding more bytes. Token arrays
are usable only after successful parsing; failure clears token count but does
not scrub scratch buffers. The caller must not expose scratch contents.

JSON-RPC batch calls and fractional/exponential numeric IDs are unsupported.
Top-level fields are restricted to jsonrpc, id, method and params. Duplicate
root fields are refused even when their spelling uses Unicode escapes. Unknown
method parameters are not executed. Nested duplicates are not a global JSON
syntax error; method-specific parameter validation must reject ambiguous keys
before any future admitted operation. Ping accepts only absent or empty-object
params. The decoder does not advertise agent capabilities or pretend unsupported
session operations work.

The seed compiler's loop fuel limits the stdio probe. It is not suitable for an
unbounded service lifetime as written. Oversized input terminates this adapter
with65; the transport-independent framer can recover at the following newline.
Actual short-write/EINTR injection is NOT_RUN; those branches have only source
inspection so far. No network listener, credentials, profiles, model service,
user memory or boot image was changed.

## Construction pin and reproduction

Compiler source commit: `0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9` in the existing
omega-bridge native developer candidate. Compiler SHA256:
`191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9`.
A copied artifact matching that pin was used, not a mutable worker's binary.
Its source-bootstrap and public developer-package provenance were inspected;
this work did not rerun the full compiler-bootstrap suite.

From repository root on Linux AArch64, with `OMC` pointing at that verified
executable and `OUT` to a private, previously empty output directory:

```sh
cat native/hermes/src/json.om native/hermes/tests/json.om > "$OUT/json.om"
"$OMC" < "$OUT/json.om" > "$OUT/json-test" && test -s "$OUT/json-test"
chmod +x "$OUT/json-test"
"$OUT/json-test"
cat native/hermes/src/json.om native/hermes/src/rpc.om native/hermes/tests/rpc.om > "$OUT/rpc.om"
"$OMC" < "$OUT/rpc.om" > "$OUT/rpc-test" && test -s "$OUT/rpc-test"
chmod +x "$OUT/rpc-test"
"$OUT/rpc-test"
cat native/hermes/src/json.om native/hermes/src/rpc.om native/hermes/src/stream.om native/hermes/tests/stream.om > "$OUT/stream.om"
"$OMC" < "$OUT/stream.om" > "$OUT/stream-test" && test -s "$OUT/stream-test"
chmod +x "$OUT/stream-test"
"$OUT/stream-test"
cat native/hermes/src/json.om native/hermes/src/rpc.om native/hermes/src/stream.om native/hermes/src/stdio.om > "$OUT/gateway.om"
"$OMC" < "$OUT/gateway.om" > "$OUT/gateway" && test -s "$OUT/gateway"
chmod +x "$OUT/gateway"
"$OUT/gateway" < native/hermes/tests/stdio.input > "$OUT/actual"
cmp native/hermes/tests/stdio.expected "$OUT/actual"
```

Run each step only after the preceding step succeeded. Compiler exit0 and a
nonempty correctly targeted ELF are prerequisites to execution. Never treat an
empty output from failed compilation as a runnable script.

## Still required for the requested product

1. Native network/host-call binding, WebSocket and authenticated Hermes endpoint.
   The inspected seed emits Linux read/write/exit calls, not Aienux calls.
2. The real Allen admission and event interface, identity/memory continuity,
   durable conversation IDs and replay/cancellation. The inspected iMac courier
   at aienos-bundle1 `03f493a` is a lab UDP model-worker protocol, not a Hermes
   control endpoint. Connecting straight to the model would bypass Allen.
3. Real Hermes client integration, not just synthetic JSON frames.
4. Aienux build/emulator/physical execution evidence. Linux native execution does
   not qualify any of these.
5. Full repository gates and independent review. Neither is claimed here.

Tracked as L690. No READY/merge/deployment request accompanies this checkpoint.
