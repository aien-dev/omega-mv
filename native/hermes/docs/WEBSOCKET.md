# WebSocket wire-codec checkpoint

Still not a network server or Allen gateway. No HTTP listener, HTTP Upgrade
parser, Origin/authentication decision, socket provider, timeout policy or Allen
session dispatch is implemented by these files. Do not expose them as a service.
The codec must run only behind an authenticated, validated HTTP Upgrade boundary.
SHA-1 here is mandated by RFC6455; it is not authentication or password hashing.

All new implementation and executable tests are Omega, compiled directly by the
same pinned native compiler documented in ../README.md. Existing shell commands
compose sources and run native tests. OpenSSL was a separately invoked reference
oracle, never linked or called by the Omega executable.

## Files and interface

- `src/wsaccept.om`: `wshake(n)` consumes `wkey[24]`; validates canonical base64
  for exactly16 decoded bytes and generates `waccept[28]`. Return0 success,
  return1 refusal; rejection leaves output bytes unchanged. No HTTP validation.
- `src/wsframe.om`: `wreset()` and `wrecv(octet)`, an incremental server decoder.
  Result0 incomplete;1 completed text in `jin[0:wnmsg]`;2 ping,3 pong,4 close
  in `wctrl[0:wclen]`. Codes1002/1003/1007/1009 are terminal protocol,
  unsupported-binary, UTF-8 and size errors. Consume completed outputs before
  more input. A close result terminates the decoder; subsequent input refuses.
  Fragmented text and interleaved control frames are supported. Client masking
  is mandatory; RSV/extensions are refused. Control frames cannot fragment or
  exceed125 bytes. Minimal length encodings and close status/reason are checked.
- `src/wsreply.om`: `wpack(op,n,source)` emits a final unmasked server frame into
  `wout[0:wolen]`. Source0 selects `rout`, source1 selects `wctrl`. Opcodes1/8/9/10
  only. Return0 success,1 refusal; refusal clears length, not scratch bytes.
  Caller supplies a validated payload and must suppress notification responses.

One connection's global state, not concurrent or reentrant. Inbound complete
text cap65,536 bytes; outbound cap131,072 bytes; control cap125 bytes. No dynamic
allocation. Text bytes are validated before dispatch, including UTF-8 codepoints
split across fragments. Close reason validation never overwrites fragmented text.
The encoder is a bounded copying primitive, not a standalone payload validator.
Production needs per-connection ownership, scheduling, admission and timeouts.

## Executed evidence

- Accept:14 native checks; initial compiling stub failed7.
- Decoder:44 native checks; initial36-check compiling stub failed32. A separately
  added high-bit length test failed1, then passed after correcting protocol-error
  precedence over message-too-large. Final suite includes exact65,536-byte input,
  overflow across fragments, nonminimal lengths, invalid UTF-8/surrogates and
  out-of-range codepoints, interleaved ping, close reason and status checks.
- Reply/integration:29 native checks; initial compiling stub failed21. Tests feed
  a masked JSON-RPC ping through decoder -> existing `rrun` -> server encoder and
  compare the complete result. Tests cover 16-/64-bit length headers, maximum
  outbound buffer and refusal. Existing RPC core emits a trailing LF (valid JSON
  whitespace); the initially LF-less expected fixture failed2 and was corrected
  to preserve the core's actual contract. Original mismatch log retained.
- Compiler issues were not behavioral REDs: `byte` parameter name is reserved
  (error16); renamed to `octet`. A deeply nested length-byte expression exhausted
  expression registers (error12); split it into typed intermediates.

No claim of exhaustive RFC compliance, independent review, real Hermes client
integration or native AienOS execution. The tests use Omega-authored frame fixtures.

Reference: https://www.rfc-editor.org/rfc/rfc6455.txt (sections4.2.2,5,7).
External oracle commands and actual results:

```sh
printf '%s' 'dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11' | openssl dgst -sha1 -binary | openssl base64
# s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
printf '%s' 'AAAAAAAAAAAAAAAAAAAAAA==258EAFA5-E914-47DA-95CA-C5AB0DC85B11' | openssl dgst -sha1 -binary | openssl base64
# ICX+Yqv66kxgM0FcWaLWlFLwTAI=
printf '%s' '/////////////////////w==258EAFA5-E914-47DA-95CA-C5AB0DC85B11' | openssl dgst -sha1 -binary | openssl base64
# XXpj4jYzLM2yUE0C7TIgMwTQh2g=
```

Reproduction source order with the same OMC/OUT prerequisites as ../README.md:

```sh
cat native/hermes/src/wsaccept.om native/hermes/tests/wsaccept.om > "$OUT/wsaccept.om"
cat native/hermes/src/json.om native/hermes/src/wsframe.om native/hermes/tests/wsframe.om > "$OUT/wsframe.om"
cat native/hermes/src/json.om native/hermes/src/rpc.om native/hermes/src/wsframe.om native/hermes/src/wsreply.om native/hermes/tests/wsreply.om > "$OUT/wsreply.om"
```

Compile each via `"$OMC" < "$OUT/NAME.om" > "$OUT/NAME-test"`, require successful
compiler status and nonempty AArch64 ELF, then chmod and execute. Expected exits0.

## Ownership and delivery dependency

L408 already owns native socket/readiness and signal APIs (FR16/17). Its worktree
was mutable and unqualified at inspection. This lane did not fork or change the
compiler or consume its mutable binary. A precise consumer supplement was appended
and read back in `handoffs/omega-feature-requests.md`. L694 owns the Omega iMac
Allen runtime; its protocol consumer contract was recorded in that ledger item.
No new agent, service, socket listener, boot change or model change was launched.
The codec does not create another Allen identity or bypass Allen to the model.
