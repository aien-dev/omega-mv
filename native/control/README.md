# Om control contracts

Refs [#659](https://github.com/aien-dev/omega-mv/issues/659),
[#649](https://github.com/aien-dev/omega-mv/issues/649) and
[#651](https://github.com/aien-dev/omega-mv/issues/651).

Three bounded Om modules implement action recovery decisions, delegation
constraints and KV reuse admission. They use caller-owned fixed arrays. They
perform no I/O and allocate no global application state. Tests are Om programs
compiled by the existing seeded compiler. There is no foreign runtime fallback.

These are component implementations awaiting integration into the existing
trusted owners. They are not a new authority root, journal, scheduler, cache,
daemon, transport or production entry point. The old active implementations
remain migration work until their consumers have qualified Om replacements.
No existing C/Rust consumer has been switched by this patch.

## Trust and integration contract

Inputs to these functions are **trusted host facts**, never unchecked model,
RPC or client assertions. In particular, `authorized=1` and `verified=1` do not
authenticate themselves. The existing capability/approval owner must derive
them from its real authorization and receipt verification. Callers serialize
each operation through their existing transaction/lock owner.

The durable effect owner must:

1. Construct exact request, operation, provider, scope, policy and grant
   identities. An operation ID names an intentional action, not just its
   arguments. Retrying that action keeps the ID; a second intentional action
   gets a new ID. The request digest covers the method, complete canonical
   arguments and relevant contract version.
2. Check current authority and resource generation under the existing execution
   fence. Call `actstart`, then durably persist the resulting in-flight record
   **before** dispatch. A failed persistence step forbids dispatch. A snapshot
   comparison alone cannot prevent revocation racing with dispatch.
3. Pass the same operation identity as the provider idempotency key when the
   provider supports that contract. An ordinary request header alone is not
   deduplication.
4. Treat a recovered in-flight record as unknown with `actlost`. An interrupted
   call, timeout, missing response or missing receipt cannot establish that the
   effect did not happen. Reconcile against the authoritative provider.
5. Persist reconciliation before exposing the next state. A terminal
   not-performed result must also guarantee that the old attempt cannot later
   complete. If the provider cannot prove that, use unknown. A read that returns
   "not found" without such a guarantee is insufficient.
6. Recheck current authority, generation, expiry and exact bindings on every
   retry. Do not change a frozen request to make a refused attempt pass.

The modules do not provide atomic disk writes, crash-consistent replay,
cross-process exclusion, cryptographic receipt verification or complete
mediation of application entry points. Those integration gates remain open
under #649/#651 and their existing owners. Hashes identify bytes; signatures,
authorization and rollback freshness remain separate obligations.

## Action record v1

All arrays below contain exactly 32 `ulong` words. A different length is a
compiler refusal, not a supported alternate wire format. These are in-memory
contracts, not a newly standardized on-disk serialization.

| Words | Stored record | Current admission facts | Reconciliation facts |
|---|---|---|---|
| 0 | Version, exactly 1 | Version, exactly 1 | Version, exactly 1 |
| 1 | State | Authorized, exactly 1 | Outcome: 0 unknown, 1 not performed, 2 performed |
| 2 | Attempt count | Current monotonic time | Exact attempt being reconciled |
| 3 | Attempt limit, 1..1024 | Reserved, zero | Receipt verified by owner, exactly 1 |
| 4 | Resource generation, nonzero | Current generation | Original generation |
| 5 | Authority epoch, nonzero | Current epoch | Original epoch |
| 6 | Exclusive expiry, nonzero | Reserved, zero | Reserved, zero |
| 7 | Reserved, zero | Reserved, zero | Reserved, zero |
| 8..11 | Operation ID | Same operation ID | Same operation ID |
| 12..15 | Exact request digest | Same digest | Same digest |
| 16..19 | Scope ID | Same scope | Same scope |
| 20..23 | Provider identity | Same provider | Same provider |
| 24..27 | Policy identity | Same policy | Same policy |
| 28..31 | Grant/approval identity | Same grant | Same grant |

Each four-word identity must be nonzero as a group; zero is the missing-identity
sentinel. Generations and epochs are supplied by their authoritative owners.
Expiry and current time must share the same clock domain. Attempt counts never
wrap; a record exceeding its limit is malformed.

| Function | Behavior |
|---|---|
| `actvalid(record)` | Validate the stored record without mutation. |
| `actstart(record, current)` | Ready -> in-flight; increment attempt only after every check. |
| `actlost(record)` | In-flight -> unknown; already unknown is idempotent. |
| `actrecon(record, receipt)` | In-flight/unknown -> ready, unknown or performed, according to the verified outcome. |

States are 1 ready, 2 in-flight, 3 unknown and 4 performed. A performed effect
is terminal even if a higher-level task failed. Successful observation of an
unknown outcome returns 21 and sets state 3. This is the one nonzero result
that changes a record; malformed or rejected inputs leave it unchanged.
Observation may record a past result after grant expiry; it grants no new work.

| Result | Meaning |
|---|---|
| 0 | Transition accepted |
| 11 / 12 | Unsupported version / malformed record or reserved field |
| 20 / 21 / 22 / 23 | Wrong state / outcome unknown / already performed / attempt limit |
| 30 / 31 / 32 / 33 | Not authorized / generation mismatch / epoch mismatch / expired |
| 35 / 36 | Wrong attempt / unverified reconciliation |
| 40..45 | Operation, request, scope, provider, policy or grant mismatch, respectively |

## Delegation v1

`capchild(parent, candidate, now)` is a predicate over two 24-word records. It
does not issue credentials. The authority owner verifies the parent, checks
revocation, reserves aggregate budgets and issues the child only after success.

| Words | Meaning |
|---|---|
| 0 | Version 1 |
| 1 | Opaque 64-bit rights set in the verified parent's authority namespace |
| 2 / 3 | Memory and work/fuel ceiling; zero grants none |
| 4 | Exclusive expiry in the same clock domain as `now` |
| 5 | Remaining delegation depth, 0..64 |
| 6 / 7 | Reserved, zero |
| 8..11 / 12..15 | Exact resource scope / authority-root identity |
| 16..19 / 20..23 | Subject / grant identity |

Rights retain their existing authority-owner meanings; effect-class labels do
not substitute for permission bits. All identities must be present. Scope and
root stay equal. Subject and grant
must change. Child rights are a subset, ceilings cannot increase, expiry cannot
extend, and depth strictly decreases. Depth zero is terminal. Comparisons avoid
increment/decrement overflow. No behavioral score participates in this contract.

Individual child ceilings do not enforce the **sum across siblings**. The
existing quota owner must reserve and account for shared budgets. The scope
profile permits only exact resource equality; path prefixes, subnets and
wildcard containment require separately qualified authority semantics.

Results: 61 version, 62 malformed, 64 parent expired, 65 child expired, 66 rights
increase, 67 memory increase, 68 fuel increase, 69 expiry increase, 70 changed
scope, 71 changed root, 72 terminal parent, 73 depth not reduced, 74 unchanged
subject, 75 unchanged grant. Zero means the predicate passed, not that a
credential was minted or that the caller is authorized.

## KV reuse v1

`kvmatch(saved, expected, actual_payload_sha256)` accepts two 40-word records
and a four-word payload digest. `expected` comes from the current trusted
consumer, not from the saved header. `actual_payload_sha256` must be computed
from the complete immutable payload by the existing digest/store owner. Merely
copying the header digest into this argument would defeat integrity checking.

| Words | Meaning |
|---|---|
| 0 | Format, exactly 1 |
| 1 | Nonzero semantic profile identifier |
| 2 / 3 | Prefix token count / context capacity; positive, prefix <= capacity |
| 4 | Nonzero cache-layout profile identifier |
| 5 | Dtype profile: 1 FP32, 2 FP16, 3 BF16, 4 quantized with complete numeric-config identity |
| 6 / 7 | Nonzero RoPE profile identifier / reserved zero |
| 8..11 | Model identity |
| 12..15 | Exact weight-content identity |
| 16..19 | Engine/compiler/build identity |
| 20..23 | Tokenizer identity |
| 24..27 | Tenant/authority scope identity |
| 28..31 | Complete prefix token-sequence identity |
| 32..35 | Numeric/target configuration identity |
| 36..39 | Payload digest in `saved`; must be zero in `expected` |

The numeric/target identity must cover every result-affecting setting, including
actual RoPE parameters, rounding, quantization scales/layout, target arithmetic
profile and relevant runtime configuration. A profile label alone is not that
identity. Tokenize the complete prompt with the bound tokenizer before choosing
a prefix; concatenating separately tokenized strings can change boundary tokens.
No missing identity receives a default. This profile refuses empty-prefix caches.

Results: 100 format, 102 reserved field, 103 invalid scalar profile/count, 104
missing identity, 111..116 scalar mismatch in words 1..6, 120..126 identity
mismatch in the table's seven groups, 127 payload digest mismatch, 128 invalid
expected payload field. Zero permits this identity/integrity comparison only.
The consumer still checks current authorization, payload bounds and supported
deserialization before exposing data. Neither this module nor a cache hit is
evidence of a speedup or a new hardware run.

## Build and evidence

Compiler source pin: `57f5f4258cebf3d5b2c22853cd062e351265c525`,
`seed/omc/omc.om`. The seed reconstruction and identical compiler stages are
recorded in [evidence/659](evidence/659/README.md). No compiler changes are needed.
On a supported AArch64 host, a module's test is built as one source unit:

```sh
cat native/control/src/action.om native/control/tests/action.om > /tmp/action.om
/path/to/pinned/omc < /tmp/action.om > /tmp/action.elf
chmod +x /tmp/action.elf
/tmp/action.elf
```

Use `delegate` and `reuse` for the other suites. Source concatenation and command
invocation here are temporary build transport, not new runtime semantics or a
project-owned shell test runner. Native import/build ownership remains with the
seeded tool owners. In the recorded x86_64 run, QEMU invoked the AArch64 compiler
and output programs. That is **EMULATED**, not Spark hardware qualification.

## Research provenance and selection

The implementation is original Om code. No external implementation was copied.

| Project reviewed | Pattern used or disposition |
|---|---|
| [Delentia](https://github.com/delentia-labs/Delentia-OS/tree/5602c9bb57106543ed799719be8d6dc79773c860) | Intent/result separation, uncertain effects, exact-payload approval, fresh authority on retry. |
| [OctantOS public docs](https://github.com/MatrixForgeLabs/OctantOS-Public-Docs/tree/35f90f4dd4f03ce77f23733af2b8a39e9a459d9b) | Parent-child attenuation. Public documentation is not an inspected enforcement implementation. |
| [Aither AWdk](https://github.com/Aitherium/awdk/tree/eca903dc7c84f98db7df72ae48d7e396e06bffc6) | Complete cache bindings and diagnostic refusals. Reuse the existing AIEN KV/store owner. |
| [SomaOS](https://github.com/avsribhas/SomaOS/tree/ef3a42afe2cca50f0047294e4a42c6bb31fd7e9d) | Shared typed application state for human/agent interfaces belongs in #648. No behavior-based authority escalation is adopted. |
| [khy](https://pypi.org/project/khy-os/1.1.7/) | Published source package reviewed separately; no code incorporated. Its source-available terms require separate permission for modification/redistribution. |
| [KIRA author's thread](https://news.ycombinator.com/item?id=44584958) | No matching public implementation located. No kernel/AGI claims adopted as implementation evidence. |

Production dispatch integration, authoritative durability, independent final
source review and hardware execution remain open. This change does not close
the parent issues or declare these projects' wider claims verified.
