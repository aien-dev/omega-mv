# om/tls: a TLS 1.3 client in Omega

A TLS 1.3 client (RFC 8446) written in seeded Omega, with every primitive it needs, for HTTPS and
SMTPS from Omega programs on Linux ARM64. No C, Rust, OpenSSL or other foreign code is in the
build or run path. Python and the `openssl` command appear only in tests, as oracles and fixture
generators.

Status: IMPLEMENTED and EXERCISED on the host (Linux ARM64, DGX Spark), 2026-10-26. These are
library units compiled from source into each program. This is not hardware qualification and
nothing is deployed.

## Units

Compose them in this order with `sh om/tls/build.sh OMC PROGRAM.om OUT`. Each unit calls only the
units before it. The composed unit is OM-2 (a leading `om2` line), and the library files have no
`fn main` and no mutable globals.

| unit | contents | fns |
|---|---|---|
| `sha2.om` | SHA-256/384/512 (FIPS 180-4), streaming; contexts are 40 `ulong` cells | 27 |
| `hkdf.om` | HMAC-SHA-256/384/512 (RFC 2104), HKDF extract/expand (RFC 5869) | 8 |
| `x25519.om` | X25519 (RFC 7748), constant-time Montgomery ladder, radix 2^25.5 | 15 |
| `chacha.om` | ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD (RFC 8439) | 19 |
| `aesgcm.om` | AES-128/192/256 bitsliced, GCM (SP 800-38D), AES-128-GCM AEAD for TLS | 37 |
| `ec.om` | ECDSA verification on P-256 and P-384 (FIPS 186-4), DER or r‖s | 27 |
| `rsa.om` | RSASSA-PKCS1-v1_5 and RSASSA-PSS verification (RFC 8017), 1024-8192-bit keys | 32 |
| `x509.om` | strict DER, X.509 v3, PEM trust store, path validation, CertificateVerify | 65 |
| `tls.om` | handshake, record layer, key schedule, alerts, DNS A lookup, TCP connect | 99 |

The total is 329 functions. omc at `befb8199` allows 512 per unit, so a program has about 180 left.

## Client API (`tls.om`)

All buffers are owned by the caller. `c` is the connection state, at least `TLCSZ` (119296) bytes.
`st` is the trust store, `TLSTSZ` (262144) bytes. Codes are `ulong`, and 0 means success.
`tls_errname code out` writes the code's name, such as `TLS_ERR_CERTIFICATE`.
`tls_detail c` gives the detail of the last failure: the `x509.om` reject code for
`TLS_ERR_CERTIFICATE` and `TLS_ERR_CERTIFICATE_VERIFY`, or the received alert number for
`TLS_ERR_ALERT_RECEIVED`.

```
e  = tls_store st                        load /etc/ssl/certs/ca-certificates.crt
e  = tls_init c hostname flags           flags 1: offer ALPN http/1.1; the name is SNI and the checked name
fd = tls_dial hostname port              DNS A + TCP (IPv4; a dotted literal skips DNS); fd or -code
tls_deadline c monotonic_ns              absolute deadline (monons) for every wait; 0 = 30 s per wait
e  = tls_connect c st int fd             full handshake; any failure sends an alert and is final
n  = tls_read c out                      > 0 bytes; 0 = close_notify received; -code on error
n  = tls_write c bytes                   all bytes or -code
tls_close c                              send close_notify, wipe keys (the caller closes fd)
```

For STARTTLS (RFC 3207), do the plaintext exchange on `fd` first, then call `tls_connect`. See
`test/tls/client.om`, mode `tsmtp`.

## What it does and refuses

- TLS 1.3 only. `supported_versions` offers only 0x0304, and a ServerHello without it is refused
  (`TLS_ERR_PROTOCOL_VERSION`). There is no TLS 1.2 code, no fallback and no insecure switch.
- Cipher suites: TLS_CHACHA20_POLY1305_SHA256 (preferred) and TLS_AES_128_GCM_SHA256.
  Key exchange: x25519 only. A server that needs another group refuses the handshake, or sends
  HelloRetryRequest, which the client refuses (`TLS_ERR_HELLO_RETRY_UNSUPPORTED`).
- The server is always authenticated, in this order:
  1. `x509_chain_verify`: path to a trust anchor, signatures, validity at the current realtime
     second, basicConstraints and pathLen, keyUsage, EKU serverAuth, critical extensions, RSA keys
     of at least 2048 bits.
  2. Hostname: SAN dNSName only, no CN fallback; a wildcard covers only the whole leftmost label.
  3. CertificateVerify over the transcript hash: ecdsa_secp256r1_sha256, ecdsa_secp384r1_sha384,
     or rsa_pss_rsae_sha256/384/512. rsa_pkcs1 is never accepted here.
  4. The server Finished HMAC, compared in constant time.
- Session IDs, extension sets and lengths are checked exactly, and duplicate or unexpected
  extensions are refused. ALPN must be the one protocol offered.
- A CertificateRequest is answered with an empty Certificate; there are no client certificates.
  NewSessionTicket is ignored (no resumption, PSK or 0-RTT). KeyUpdate is honoured, including
  update_requested.
- Records: 64-bit sequence numbers, never wrapped (`TLS_ERR_SEQUENCE_LIMIT`). A record over
  2^14 + 256 is refused. Plaintext is released only after its tag verifies. An end of TCP without
  close_notify is `TLS_ERR_EOF`, never a clean 0.
- Not implemented: revocation (OCSP, CRL), Certificate Transparency, IPv6, DNSSEC, DNS over TCP,
  RFC 5280 name canonicalisation (issuer names are matched as exact DER bytes), and nameConstraints
  (a critical one is refused).
- DNS is unauthenticated (UDP to the first IPv4 nameserver in `/etc/resolv.conf`, random 16-bit
  ID, question echo checked). A spoofed answer can redirect the TCP connection but cannot pass
  server authentication.

## Side channels: what is and is not constant time

Constant time here means that no branch, loop count or memory index depends on a secret.

| unit | secret data | constant time? |
|---|---|---|
| `x25519.om` | scalar, shared secret | yes: fixed-count ladder, masked cswap, masked final reduction and zero check |
| `chacha.om` | key, keystream, Poly1305 state, tag | yes: ARX only; tag compare ORs all 16 bytes before deciding |
| `aesgcm.om` | key, round keys, H, data | yes: bitsliced AES (BearSSL `aes_ct64` layout, MIT, T. Pornin; `NOTICE.bearssl`), Boyar-Peralta depth-16 S-box circuit, masked GHASH; no table lookups |
| `sha2.om`, `hkdf.om` | HMAC keys, PRKs, secrets | yes: branches only on lengths and the algorithm |
| `tls.om` | traffic secrets, keys, Finished | the key schedule, nonce and Finished compare (`tls_cteq`) are constant time. Branches depend on public values (suite, lengths, content type) |
| `ec.om`, `rsa.om`, `x509.om` | none (verification of public data) | no, by design. Do not reuse them for signing or ECDH |

Not covered, or covered only partly:

- **CPU timing.** omc emits plain AArch64. The constant-time claims rely on MUL, ADD, EOR, AND,
  ORR and the shifts having data-independent timing. The DIT bit is not set.
- **Record padding.** The inner-plaintext padding is stripped by a backward scan, so its time
  reveals the padding length. RFC 8446 section 5.4 accepts this.
- **Lengths and outcomes.** Message lengths, record counts and whether a tag check failed are
  visible, as in every TLS.
- **Wiping.** Secrets in registers, spilled stack slots and caller-owned buffers are not scrubbed
  by the compiler. The units zero their own secret local arrays, and `tls_close` and failures
  zero the keys in `c`.
- **Conversions.** A `uchar` conversion of a secret value compiles to a range check whose branch
  is never taken.
- **No measurement proof.** No dudect-style statistical timing test has been run. The x25519
  timing figures in its test output are informational and were noisy on a shared host.

## Tests (run from the repository root)

| command | final line observed 2026-10-26 (omc `befb8199`) |
|---|---|
| `sh om/tls/test/sha2/run.sh OMC` | `SHA2 PASS vectors=1777 (wycheproof=774) mutants=5/5` |
| `sh om/tls/test/x25519/run.sh OMC` | `X25519 PASS vectors=531 (wycheproof=518) mutants=7/7` |
| `sh om/tls/test/chacha/run.sh OMC` | `CHACHA PASS vectors=3087 (wycheproof=339) mutants=5/5` |
| `sh om/tls/test/aesgcm/run.sh OMC` | `AESGCM PASS vectors=47570 (fips197=4, nist=47250, wycheproof=316) mutants=5/5` |
| `sh om/tls/test/ec/run.sh OMC` | `EC PASS vectors=3984 (wycheproof=3748 cavp=180 edge=56) mutants=6/6` |
| `sh om/tls/test/rsa/run.sh OMC` | `RSA PASS vectors=6486 (wycheproof=5250) mutants=4/4` |
| `sh om/tls/test/x509/run.sh OMC` | `X509 PASS cases=78 (openssl-parity=66/78) mutants=4/4` |
| `TLS_NET=1 sh om/tls/test/tls/run.sh OMC` | `TLS PASS vectors=24 (rfc8448) interop=23 net=PASS mutants=9/9` |

How the suites are built:

- **Wycheproof.** Vectors come from C2SP/wycheproof `d17cc89fa5c8f4f4d378b5385ebf75da5d0926c9`,
  and every test of each in-scope file is run. Each suite prints the files it does not run, and
  why, as NOT_RUN: SHA-1, SHA-224, SHA-3, SHAKE, other curves, XChaCha20, AES-GCM-SIV, RSA
  encryption. Wycheproof has no X.509 path vectors.
- **Other vectors.** NIST CAVP SHAVS, GCM and ECDSA SigVer; RFC 7748, 8439, 4231, 5869 and 8448.
- **Mutants.** Each suite has library mutants that must be caught as MISMATCH (exit 1; a trap does
  not count), plus an expectation mutant and flip-a-byte negative controls.
- **`test/tls`.**
  - The RFC 8448 section 3 KAT checks ECDHE, every secret, the keys and IVs, both Finished values,
    and record seal and open byte for byte.
  - `interop.sh` runs 23 rows against `openssl s_server` and crafted servers, all on 127.0.0.1.
  - `TLS_NET=1` adds the live checks. The handshakes reach api.github.com (ECDSA P-256 chain),
    raw.githubusercontent.com (RSA, rsa_pss_rsae) and mail.gandi.net on 465 and on 587 with
    STARTTLS. Parity means the fetched bytes are compared with Python `ssl` for read-only GETs
    (bodies up to 680548 bytes) and for an EHLO/QUIT dialogue.

The interop refusal table (`build/tls/tls/interop-table.txt`):

| case | outcome |
|---|---|
| good EC chain (ChaCha20, AES-128-GCM), good RSA leaf | accepted |
| expired, not-yet-valid, wrong name | `TLS_ERR_CERTIFICATE` with `X509_ERR_EXPIRED`, `NOT_YET_VALID`, `NAME_MISMATCH` |
| self-signed leaf (CA:TRUE / CA:FALSE) | `X509_ERR_LEAF_IS_CA` / `X509_ERR_UNKNOWN_ISSUER` |
| bad certificate signature | `X509_ERR_BAD_SIGNATURE` |
| unknown issuer, missing intermediate | `X509_ERR_UNKNOWN_ISSUER` |
| TLS 1.2-only, AES-256-only, P-256-only server | the server's own alert 70 / 40 / 40 (`TLS_ERR_ALERT_RECEIVED`) |
| crafted ServerHello: TLS 1.2, HRR, AES-256 suite, session id, P-256 share, zero share, duplicate extension, trailing byte | `PROTOCOL_VERSION`, `HELLO_RETRY_UNSUPPORTED`, `CIPHER_SUITE`, `SESSION_ID_ECHO`, `KEY_SHARE`, `ZERO_SHARED_SECRET`, `EXTENSION`, `DECODE` |
| one bit flipped in a server record by a proxy | `TLS_ERR_BAD_RECORD_MAC` |

## Writing another unit for this set

- Every function name carries its unit prefix and a `_`.
- Constants and texts are UPPERCASE with a two-letter unit code.
- Locals are at most 8 characters and must not collide with any composed name or host word.
- A function takes at most 6 parameter slots; an unsized array uses 2.
- Error 12 means the function needs more than 16 registers: split it.
- Untrusted input must never trap. Check every length before indexing.
