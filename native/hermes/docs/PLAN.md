# Allen Hermes gateway — Omega implementation plan

User direction: keep Hermes desktop as the interface, reach the real Allen hosted by AienOS, implement personally and entirely in Omega. No Rust-first scaffold. User authorized continuing after the language correction.

## Architecture and non-goals
Hermes JSON-RPC is a compatibility boundary, not a second agent runtime. Allen identity, memory and execution authority remain with existing owners. Implement a bounded transport-independent protocol core in seeded Omega, then native transport bindings and the existing Allen admission interface. Unsupported methods must return explicit errors. Never claim protocol-unit success as desktop or AienOS qualification. Do not replace the running model, edit another profile, reboot, or deploy an unauthenticated listener.

## Frozen inputs
- Hermes local source contracts: tui_gateway/contracts and apps/shared/src/gateway-contract.openrpc.json; preserve JSON-RPC 2.0 request IDs, errors, notification semantics and event names.
- Seeded compiler: omega-bridge commit 0fc7d8662cb799e7c206b17c18ea4c3ec2b8fea9, native omc SHA256 191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9. Copied to private task output; never consume a mutable sibling binary during builds.
- Scope repository: omega, owned branch feat/allen-hermes-omega-L690. New module native/hermes only; no compiler owner files changed.
- Current pinned compiler emits Linux AArch64 ELF with read/write/exit host calls; it does not establish an Aienux transport binding or native socket support. Those are explicit downstream tasks, not hidden foreign-runtime substitutes.

## Tasks
1. Strict bounded JSON codec in src/json.om, tests/json.om. Input pool 65536 bytes; fixed token table 4096; nesting depth limit 32. Token spans retain original bytes and parent relationship. Reject malformed strings/escapes, noncanonical JSON number syntax, trailing bytes, invalid UTF-8, and excessive depth/tokens. Test executable written in Omega, real failing stub first, compiler exit/nonempty ELF checked before running. No production-language test generator.
2. JSON-RPC dispatcher in src/rpc.om, tests/rpc.om. Validate object envelope/version/method/id and distinguish notifications; preserve string/integer IDs exactly, reject duplicate keys and malformed parameters; implement ping and explicit method-not-found, not fake session/model answers. Tests cover quoted method text inside user content, split input and failure recovery. No mutation on refusal.
3. Native stream driver src/stdio.om. Same framing as Hermes stdio, bounded messages, partial reads/writes and EOF behavior. Real client verification uses the existing unmodified Hermes client as an external oracle, not a newly written JS adapter. This is protocol qualification only.
4. Native transport and authority attachment. Inventory current Aienux and Interplane interfaces and reuse them; add an Omega host-call provider only where missing. WebSocket handshake/frame handling, authentication, origin checks, limits, cancellation, backpressure and replay tested before opening any non-loopback endpoint. No shipping C/Rust/Python transport shim. Kernel ABI and Linux ABI must be distinct.
5. Allen session admission, streaming and resume. Existing identity/authority binding; durable request IDs, interrupted/unknown outcomes, bounded replay, no duplicate work on reconnect. Tests against actual owner service, not an echo model. Preserve memory lineage and capability authorization.
6. Actual Hermes connection qualification plus AienOS integration/emulation and physical qualification separately. User-visible first acceptance: exchange messages, disconnect/reconnect and resume the same Allen conversation. Source review, exact-source evidence and sole-desk publication handoff; no auto boot or service replacement.

## Review focus
Malformed or duplicate JSON keys; oversized/deep/fragmented inputs; disconnect after admission before response; foreign identity or revoked auth on reconnect; unsupported frontend methods that silently create misleading state.

## Progress
L690 is the durable coordination item. Whole feature remains OPEN until actual client/Allen/AienOS acceptance, regardless of protocol test results.
