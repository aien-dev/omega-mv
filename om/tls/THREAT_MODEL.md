# om/tls and seed/net: threat model

Written for the outside security audit (branch `audit/tls-outside-review`, base omega-mv main
`705c0f01`, omc `189ada82`). Facts here are what the code does at that revision; levels are
IMPLEMENTED (code exists), EXERCISED (a test ran on the Spark host), MEASURED (numbers from a run),
never QUALIFIED (no third-party or hardware qualification exists).

## Assets

1. The ECDHE private scalar, the shared secret, every handshake and traffic secret, the record keys
   and IVs (`c[TLEPK..TLEPU)`, `c[TLSS]`, `c[TLHS..TLCRX)`, `c[TLWKEY..TLHOST)`).
2. Plaintext in the record buffers (`c[TLRB..TLCSZ)`) and in caller buffers.
3. The authenticity of the peer: the chain in the caller's trust store (`tls_storef`), host name
   (`tls_init`), CertificateVerify and Finished.
4. Availability of the calling program: no trap on hostile input, bounded memory (`TLCSZ`,
   `TLSTSZ`), bounded time (deadlines).

## Attacker

- Network attacker on the path: reads, injects, reorders, truncates, delays every byte; runs the
  server (`fakesrv.py` models this); controls DNS answers (`dnsunit.om` models this).
- Not in scope: a local attacker on the same host (ptrace, /proc/PID/mem, core dumps, swap), a
  compromised trust store, a compromised compiler or `seed/out/omc`, physical attacks, power or
  electromagnetic side channels, speculative-execution side channels.
- Partly in scope: a co-tenant measuring timing. The units are written to be constant time in
  secret data (README `Side channels`); the measurement below is host-only evidence.

## Mitigations and their evidence

| threat | mitigation | evidence |
|---|---|---|
| forged server | chain verification to the caller's roots, name check, CertificateVerify over the transcript, Finished over the transcript (`x509.om`, `tls.om`) | interop rows `cvbad`, `cvkey`, `cvskip`, `badchain*`; mutants chain result ignored, CertificateVerify ignored; EXERCISED |
| downgrade | TLS 1.3 only (`supported_versions` checked, `TLS_ERR_VERSION`); suites x25519 + ChaCha20-Poly1305 / AES-128-GCM only | mutant version not checked; EXERCISED |
| tampered records | AEAD with the TLS 1.3 nonce; tag failure is fatal (`TLS_ERR_BAD_RECORD_MAC`) | RFC 8448 KAT, mutant record MAC ignored; EXERCISED |
| key overuse | 23726566 records per key and direction: proactive KeyUpdate at limit-1 on write, `TLS_ERR_SEQUENCE_LIMIT` at the limit on both directions (`tls_open`, `tls_seal`, `tls_kusend`) | interop `kuexact`, `kurot`, `kuover`, `kuw:8` (test-only `tls_reclimit`); three mutants; EXERCISED |
| slow or stalled peer | caller deadline on every call; whole-handshake budget 30 s (`tls_hslimit`); record and message caps | interop `loris`, `slowsh`; mutant handshake budget not armed; EXERCISED |
| hostile DNS | flags, owner names, label and pointer caps; no trap | `dnsunit.om` 23 cases, 4 mutants; EXERCISED |
| memory exhaustion | fixed `TLCSZ` state, `TLSTSZ` trust store, 16 KiB bounce in `https.om`, element caps in x509 | x509 `leaf_too_big`, mutants read/handshake record cap; EXERCISED |
| secrets left in memory | units zero their secret locals; `tls_close`/`tls_abort` run `tls_wipe` (keys, ephemeral key, handshake secrets, record buffers); `tls_hskeys` zeroes the shared secret, `tls_cfin` the handshake secrets | `test/tls/residue.py` (`/proc/PID/mem` scan: 12 handshake-only secrets absent after the handshake, 18 absent after close; app keys present after the handshake as the positive control); mutants close keeps the traffic secrets, handshake secrets kept, shared secret kept; MEASURED `RESIDUE PASS secrets=18 hs_only=12` |
| timing | constant-time units (README table) | dudect-style Welch t-test, `test/tls/timing.om`: all 16 |t| values < 2.5 on two cores, v2 run 2026-10-26; MEASURED host-only (see "Timing measurement") |
| HTTPS misuse | `seed/net/https.om` refuses plaintext fall-back on a TLS port, maps EOF without close_notify to -604, wipes keys in `htlsend` | `seed/net/tests/check.py` stage 5: 9 rows, 5 mutants; `TLS_NET=1` live GETs; EXERCISED |

## Known limits (not mitigated)

1. **Registers and spilled stack slots.** omc does not scrub registers or spilled locals on
   return. The residue scan looks at the whole writable address space of the process after the
   calls return, so it would see a spilled secret that survived; it saw none at the scanned
   points, but a scan is not a proof. A compiler-level wipe (zeroing the frame on return from
   functions that hold secrets) is a proposal for the compiler lane, not implemented.
2. **PSTATE.DIT is not set.** `tls_hwdit` reports whether FEAT_DIT is advertised (HWCAP bit 24); the
   library cannot execute `msr DIT, #1`. Probe (MEASURED, `evidence/dit-probe.log` in the handoff):
   `mrs x0, DIT` = `d53b42a0`, `msr DIT, #1` = `d503415f`, `msr DIT, #0` = `d503405f`; Linux starts
   the bit at 0, setting it from user space works and the kernel preserves it. Proposal for omc: a
   word `dit v` (v in {0,1}) lowering to those two encodings, with `tls_connect` setting it for the
   handshake. Until then the constant-time claim rests on the instruction set used (no table
   lookups, no secret-dependent branches) and on the measurement.
3. **Revocation is not checked**: no OCSP, no stapling, no CRL (README `Policy`).
4. **Padding length and record lengths** are visible (RFC 8446 section 5.4).
5. **Caller buffers**: plaintext the caller receives from `tls_read` is the caller's to wipe.
6. **The random source** is Linux `getrandom` (blocking flag 0); no further entropy check.
7. **Single host, single toolchain**: every result above was produced on one DGX Spark (arm64,
   GB10) with the pinned omc; the hosted arm64 CI job reruns the suites on a second machine, but the
   compiler binary is the same pinned one.

## Timing measurement

`om/tls/test/tls/timing.om` times one operation per sample with `monons` after preparing an input
whose class (fixed pattern or `getrandom` bytes) is drawn per sample; both classes call `getrandom`
before the timed window so the cache and TLB state does not differ by class (the first driver did
not, and its cropped ChaCha20 rows produced |t| of 10.4 and 5.1, which vanished once the classes
were equalized; recorded in the evidence). Welch's t statistic was computed in Wolfram from per-class
counts and sums (full data and cropped at the pooled 90th percentile). Raw samples, sums, the binary
and the queries: `/home/drakestapleton/handoffs/TlsAudit/evidence/timing/` (`welch.md`). Reproduce:
`sh build/tls/tls/client/build.sh seed/out/omc om/tls/test/tls/timing.om build/tls/tls/timing` then
`nice -n 19 taskset -c 5 build/tls/tls/timing chacha 50000`.
