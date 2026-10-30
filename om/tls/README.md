# om/tls: a TLS 1.3 client in Omega

A TLS 1.3 client (RFC 8446) written in seeded Omega, with every primitive it needs, for HTTPS and
SMTPS from Omega programs on Linux ARM64. No C, Rust, OpenSSL or other foreign code is in the
build or run path. Python and the `openssl` command appear only in tests, as oracles and fixture
generators.

Status: IMPLEMENTED and EXERCISED on the host (Linux ARM64, DGX Spark), 2026-10-26. These are
library units compiled from source into each program. This is not hardware qualification and
nothing is deployed.

## Units

Compose the client units in this order with `sh om/tls/build.sh OMC PROGRAM.om OUT`. Each unit
calls only the units before it. `gcmsiv.om` is not in that composition. The composed unit is OM-2
(a leading `om2` line), and the library files have no `fn main`. The one mutable global is `FPST`
in `fips.om`, the process-wide self-test and error state (a FIPS 140-3 module error state must cover
every connection of the process).

| unit | contents | fns |
|---|---|---|
| `sha2.om` | SHA-256/384/512 (FIPS 180-4), streaming; contexts are 40 `ulong` cells | 27 |
| `hkdf.om` | HMAC-SHA-256/384/512 (RFC 2104), HKDF extract/expand (RFC 5869) | 8 |
| `drbg.om` | HMAC_DRBG with SHA-256 (SP 800-90A Rev. 1 section 10.1.2), reseed counter, continuous test | 14 |
| `x25519.om` | X25519 (RFC 7748), constant-time Montgomery ladder, radix 2^25.5 | 15 |
| `sha3.om` | SHA3-256/512, SHAKE128/256 (FIPS 202), Keccak-f[1600] | 20 |
| `mlkem.om` | ML-KEM-768 (FIPS 203): key generation from a seed, encapsulation, decapsulation with implicit rejection | 43 |
| `chacha.om` | ChaCha20, Poly1305, ChaCha20-Poly1305 AEAD (RFC 8439) | 19 |
| `aesgcm.om` | AES-128/192/256 bitsliced, GCM (SP 800-38D), AES-128-GCM AEAD for TLS | 37 |
| `gcmsiv.om` | AES-128/256-GCM-SIV (RFC 8452); calls `aesgcm.om`; not in the TLS client build | 24 |
| `ec.om` | ECDSA verification on P-256 and P-384 (FIPS 186-4), DER or r‖s | 50 |
| `ecdh.om` | constant-time P-256/P-384 ECDH (SP 800-56A Rev. 3): key generation, partial public-key validation, Z, pair-wise consistency test | 18 |
| `rsa.om` | RSASSA-PKCS1-v1_5 and RSASSA-PSS verification (RFC 8017), 1024-8192-bit keys | 32 |
| `x509.om` | strict DER, X.509 v3, PEM trust store, path validation, CertificateVerify | 81 |
| `rev.om` | revocation: SHA-1 for OCSP CertID only, stapled OCSP (RFC 6960), must-staple (RFC 7633), caller-supplied CRL (RFC 5280); composed after `x509.om` | 61 |
| `fips.om` | self-tests (KATs), SP 800-90B health tests on getrandom, approved-mode DRBG wiring, X25519 pairwise consistency test, module error state | 38 |
| `tls.om` | handshake, record layer, key schedule, alerts, DNS A lookup, TCP connect, approved-only mode, X25519MLKEM768 hybrid key exchange | 149 |

The client composition is 612 functions. `gcmsiv.om` adds 24, so the table total is 636. omc at
`2c824805` (the batch 2 pin, `OMC_PIN` in `om/units.sh`, which
the ec, rsa and x509 suites check) allows 4095 functions per unit, as `189ada82` did.

## Client API (`tls.om`)

All buffers are owned by the caller. `c` is the connection state, at least `TLCSZ` (121696) bytes.
`st` is the trust store, `TLSTSZ` (262144) bytes. Codes are `ulong`, and 0 means success.
`tls_errname code out` writes the code's name, such as `TLS_ERR_CERTIFICATE`.
`tls_detail c` gives the detail of the last failure: the `x509.om` reject code for
`TLS_ERR_CERTIFICATE` and `TLS_ERR_CERTIFICATE_VERIFY`, or the received alert number for
`TLS_ERR_ALERT_RECEIVED`, or the number of the failed self-test for `TLS_ERR_SELFTEST`
(section "Self-tests and approved mode").

```
e  = tls_store st                        load /etc/ssl/certs/ca-certificates.crt
e  = tls_init c hostname flags           flags 1: offer ALPN http/1.1; 4: approved mode (HMAC_DRBG); 8: stapled OCSP + must-staple (rev.om);
                                         the name is SNI and the checked name
fd = tls_dial hostname port              DNS A + TCP (IPv4; a dotted literal skips DNS); fd or -code
tls_deadline c monotonic_ns              absolute deadline (monons) for every wait; 0 = 30 s per wait
                                         and, for the handshake, the whole-handshake budget below
                                         (CPU time is never covered)
e  = tls_hslimit c ns                    whole-handshake budget when no deadline is set (default 30 s;
                                         before tls_connect only)
e  = tls_reclimit c n                    tests only: lower the per-key record limit (1 .. 23726565;
                                         before tls_connect only; it can never be raised)
e  = tls_connect c st int fd             full handshake; any failure sends an alert and is final
n  = tls_read c out                      > 0 bytes; 0 = close_notify received; -code on error
                                         (an empty `out` is -TLS_ERR_ARGUMENT; the connection stays up)
n  = tls_write c bytes                   all bytes or -code; sends KeyUpdate before the write key's
                                         record limit (section "Key lifetime")
tls_close c                              send close_notify, wipe keys (the caller closes fd); a failed
                                         connection stays failed, so a later tls_read is -code, never 0
```

Handshake budget: when the caller has set no deadline, `tls_connect` arms one of now + 30 s
(`TLHSNS`, or the value given to `tls_hslimit`) over the whole handshake and clears it when the
handshake succeeds, so a server that trickles its ServerHello one byte at a time (interop rows
`loris` and `slowsh`) cannot hold the client past the budget; the usual 30 s per wait then applies
to application data. A caller deadline replaces the budget.

Key lifetime (RFC 8446 section 5.5): at most `TLKUREC` = 23726566 = floor(2^24.5) records are
protected under one key in each direction (the AES-GCM bound; ChaCha20-Poly1305 would allow more
and gets the same conservative value). `tls_write` sends KeyUpdate(update_not_requested) and
rotates its own key before the record that would be number `TLKUREC` - 1 under the current key;
`tls_seal` and `tls_open` refuse with `TLS_ERR_SEQUENCE_LIMIT` (fail closed, the connection is
failed) when a sequence number reaches the limit, which happens on the read side when the peer
never updates its key. `tls_reclimit` lowers the limit for the tests (interop rows `kuexact`,
`kuover`, `kurot`, `kuw:8`); the limit cannot be raised.

Secret wiping: `tls_close` and `tls_abort` zero the traffic keys and IVs, the application
secrets, the handshake secrets and ephemeral key, and the record, write and line buffers of `c`
(`tls_wipe`). Secrets held in function locals (the Omega compiler keeps small values in registers
and `new` arrays on the stack) are not scrubbed after a function returns; see
`THREAT_MODEL.md`.

Approved-only mode (`tls_init` flag 4, opt-in; the default mode is unchanged). The client then
offers and accepts only NIST-approved algorithms:

- cipher suite `TLS_AES_128_GCM_SHA256` alone (AES, FIPS 197; GCM, SP 800-38D; SHA-256, FIPS
  180-4; HMAC, FIPS 198-1; the TLS 1.3 key schedule, SP 800-135 Rev. 1 / SP 800-56C Rev. 2);
- groups secp256r1 and secp384r1 with a key share for each (`ecdh.om`, ECC CDH of SP 800-56A
  Rev. 3 section 5.7.1.2 with the partial public-key validation of section 5.6.2.3.4);
- signature_algorithms `ecdsa_secp256r1_sha256`, `ecdsa_secp384r1_sha384`, `rsa_pss_rsae_sha256`,
  `rsa_pss_rsae_sha384`, `rsa_pss_rsae_sha512` (FIPS 186-5).

A ServerHello choosing ChaCha20-Poly1305 is `TLS_ERR_CIPHER_SUITE`; one choosing x25519 or any
other group, or carrying an invalid point, is `TLS_ERR_KEY_SHARE`; HelloRetryRequest stays
refused. Each ephemeral private key is drawn by rejection sampling (FIPS 186-5 A.2.2, SP 800-56A
Rev. 3 5.6.1.2.2) from the approved-mode HMAC_DRBG (`tls_rand`, below) and checked with the
pair-wise consistency test of section 5.6.2.1.4; a failed test, or 16 rejected draws, latches the
module error state (detail 21) and returns `TLS_ERR_SELFTEST`. Both private keys and Z are zeroed
once the handshake secret is derived. Certificate chains may still be signed with
RSASSA-PKCS1-v1_5 (`x509.om`), and no module boundary has been submitted to a laboratory. The units
are tested against the FIPS 140-3 / CAVP test methods (CAVP vectors in each suite), not
NIST-validated. Test: `test/tls/client.om` OPTS field APPROVED, interop rows marked `0,0,0,0,1`;
`test/fips/run.sh` checks that key generation advances the DRBG and that a failed generation
latches (`driver ecgenerr`).

For STARTTLS (RFC 3207), do the plaintext exchange on `fd` first, then call `tls_connect`. See
`test/tls/client.om`, mode `tsmtp`.

## Self-tests and approved mode (`drbg.om`, `fips.om`)

Tested against the FIPS 140-3 / CAVP test methods, not NIST-validated: no module boundary has
been submitted to a laboratory, there is no CMVP certificate, and nothing here is an ESV.

- **Pre-operational and first-use self-tests.** The first `tls_connect` of the process runs
  known-answer tests (`fips_selftest`): SHA-256/384/512, HMAC-SHA-256/384, HKDF-SHA-256, the TLS 1.3
  key derivation (RFC 8448), AES-128-GCM seal and open (CAVP gcmEncryptExtIV128), ECDSA P-256
  verify with a must-fail case, RSA PKCS#1 v1.5 and PSS verify, ChaCha20-Poly1305 and X25519. In
  approved mode the HMAC_DRBG KAT (CAVP HMAC_DRBG SHA-256, no prediction resistance) runs too.
  A failure latches the module error state: `tls_connect` returns `TLS_ERR_SELFTEST` (code 32)
  before any I/O, `tls_detail` names the test (1-3 SHA, 4-5 HMAC, 6 HKDF, 7/8 GCM seal/open,
  9 ECDSA, 10 PKCS#1, 11 PSS, 12 ChaCha20-Poly1305, 13 X25519, 14 DRBG, 15 TLS key derivation,
  16 software integrity), and `tls_seal` and `tls_open` refuse with the same code for every
  connection of the process.
- **Software integrity test (detail 16).** After the known-answer tests (which cover SHA-256 and
  HMAC-SHA-256 first), `fips_integ` recomputes HMAC-SHA-256 over `/proc/self/exe` and compares it
  with the 64-hex-digit slot after the marker `omega-tls-integrity-v1-hmac-sha256:` in the
  program. `build.sh` writes that value after compiling (an Omega tool, `fips_itool`, composed from
  the same units; `build.sh -p OMC BIN` patches a program composed elsewhere). The slot is taken as
  64 '0' digits while hashing. An unpatched program (all '0') refuses. The key
  `omega-tls module integrity key v1` is public, as in OpenSSL's FIPS provider: this detects
  corruption and accidental modification of the file, not an attacker who can rewrite it, and it
  hashes the file on disk, not the mapped image. `test/integrity/run.sh` checks it against an
  independent openssl HMAC and with changed bytes, an appended byte, an unpatched build and code
  mutants.
- **Show-version service.** `tls_version o` (`fips_version`) writes
  `Omega TLS module (om/tls) version 0.1.0 integrity hmac-sha256:HEX state S` and a newline, S being
  0 before the self-tests, 1 after they passed and 2 in the error state; it returns the length, or
  0 when `o` is shorter than the line.
- **Conditional tests.** X25519 pairwise consistency (the ephemeral public key is recomputed and
  compared in constant time; detail 21), the P-256/P-384 pair-wise consistency test and rejection
  sampling limit of approved-mode key generation (detail 21), the DRBG continuous test and error
  state (detail 20),
  and the SP 800-90B section 4.4 repetition count and adaptive proportion tests on every getrandom
  byte that feeds the DRBG (detail 22 RCT, 23 APT, 24 getrandom error, 25 DRBG call error), with a
  1024-sample start-up test. Cut-offs for a claimed H = 6 bits per sample (below every SP 800-90B
  estimate measured on getrandom; the lowest was 6.638) and alpha = 2^-40: RCT 8, APT window 512,
  cut-off 35 (MODELED: the Wolfram query is in the `fips.om` notes above `FHRCT1`).
- **Approved mode (flag 4).** `tls_init` flag 4 draws the client random, session id and the
  P-256/P-384 ephemeral keys from an HMAC_DRBG (SHA-256, 48-byte entropy input + 32-byte nonce, personalization string,
  prediction resistance: a 48-byte health-tested reseed before every request) held at
  `c[TLDRBG]` and wiped by `tls_close`. Without flag 4 the bytes come straight from getrandom, as
  before. The DNS query id is not security relevant and always uses getrandom.
- **Entropy.** `test/fips/entropy.sh` runs the NIST SP 800-90B reference tool on 1,000,000
  getrandom bytes and on 1000 restarts of 1000 bytes. It grades the Linux kernel's getrandom
  output, which the kernel has already conditioned; it is not an Omega noise source, and an ESV
  would need a kernel/platform entropy certificate.

## What it does and refuses

- TLS 1.3 only. `supported_versions` offers only 0x0304, and a ServerHello without it is refused
  (`TLS_ERR_PROTOCOL_VERSION`, alert protocol_version). A supported_versions that selects any
  other version is `TLS_ERR_ILLEGAL_PARAMETER` (alert illegal_parameter, RFC 8446 4.2.1); one whose
  body is not 2 bytes is `TLS_ERR_DECODE`. There is no TLS 1.2 code, no fallback and no insecure
  switch.
- Cipher suites: TLS_CHACHA20_POLY1305_SHA256 (preferred) and TLS_AES_128_GCM_SHA256.
  Key exchange: X25519MLKEM768 (0x11EC, draft-ietf-tls-ecdhe-mlkem; ML-KEM-768 then x25519,
  share 1216 bytes, server share ct 1088 ‖ x25519 32, 64-byte (EC)DHE input to HKDF-Extract)
  and x25519, both offered in `supported_groups` and shared in ClientHello1 (FLAGS 2: the hybrid
  share only, so an x25519-only server must send a HelloRetryRequest). The ML-KEM key pair is
  generated before the ClientHello and the decapsulation key is wiped after decapsulation, or
  when the server chooses x25519.
  HelloRetryRequest (RFC 8446 4.1.4) is handled once: it must carry supported_versions 0x0304,
  each extension at most once and only supported_versions, key_share and cookie (another
  extension of the RFC 8446 4.2 table is `TLS_ERR_ILLEGAL_PARAMETER`, an unknown one
  `TLS_ERR_EXTENSION`), and change the ClientHello (`TLS_ERR_HELLO_RETRY` otherwise); its
  key_share must name a group the client offered but did not share (`TLS_ERR_KEY_SHARE`); a cookie
  is 1 .. 12000 bytes (`TLS_ERR_MESSAGE_TOO_BIG` above) and echoed byte for byte. The transcript
  restarts as message_hash(ClientHello1) + HelloRetryRequest; ClientHello2 keeps the random and
  session id; a second HelloRetryRequest is `TLS_ERR_UNEXPECTED_MESSAGE` and a ServerHello suite
  other than the HelloRetryRequest's is `TLS_ERR_CIPHER_SUITE`. A CCS may follow the
  HelloRetryRequest (RFC 8446 D.4).
- The server is always authenticated, in this order:
  1. `x509_chain_verify`: path to a trust anchor, signatures, validity at the current realtime
     second (the host clock decides), basicConstraints and pathLen, keyUsage, EKU serverAuth,
     critical extensions, RSA key policy (below). Only self-issued store entries (subject DER =
     issuer DER) are trust anchors; an intermediate placed in the store is ignored, as OpenSSL
     does without `-partial_chain`. A certificate is valid for notBefore <= now < notAfter: at
     notAfter itself it is already expired, as OpenSSL (RFC 5280 4.1.2.5 includes notAfter).
  2. Hostname: SAN dNSName only, no CN fallback; a wildcard covers only the whole leftmost label.
     There is no public-suffix check (`*.co.uk` relies on CA policy).
  3. CertificateVerify over the transcript hash: ecdsa_secp256r1_sha256, ecdsa_secp384r1_sha384,
     or rsa_pss_rsae_sha256/384/512. rsa_pkcs1 is never accepted here.
  4. The server Finished HMAC, compared in constant time.
- RSA key policy. A key the server presents (leaf, intermediate, and the CertificateVerify key)
  must have a 2048-4096-bit modulus and an odd public exponent in 3 .. 2^32-1, else
  `X509_ERR_WEAK_KEY`. This bounds the verification cost per certificate. Trust anchors need only
  2048 bits or more.
- Certificate size limits: a leaf larger than 16896 bytes, or a certificate with more than 9999
  elements in any list (extensions, names, SAN, EKU, RDNs), is refused (`X509_ERR_MALFORMED`)
  instead of trapping. issuerUniqueID and subjectUniqueID may each appear once, in that order.
- nameConstraints is not implemented, so a certificate carrying it, critical or not, is refused
  with `X509_ERR_NAME_CONSTRAINTS` (reject code 20).
- Session IDs, extension sets and lengths are checked exactly, and duplicate or unexpected
  extensions are refused. ALPN must be the one protocol offered. An extension type of the RFC 8446
  4.2 table in a message the table does not list it for (server_name or cookie in a ServerHello,
  key_share or supported_versions in EncryptedExtensions) is `TLS_ERR_ILLEGAL_PARAMETER`, alert
  illegal_parameter; an unknown type, or one allowed there that the client did not ask for, is
  `TLS_ERR_EXTENSION`, alert unsupported_extension (RFC 8446 4.2). A Certificate with an empty
  certificate_list is `TLS_ERR_DECODE`, alert decode_error (4.4.2.4).
- A CertificateRequest is answered with an empty Certificate; there are no client certificates.
  Its extensions are not validated (the empty Certificate makes them irrelevant).
  NewSessionTicket is parsed (RFC 8446 4.6.1: lifetime at most 604800 s, else `TLS_ERR_TICKET`;
  a non-empty ticket; the extension list fills the message exactly, else `TLS_ERR_DECODE`) and
  then dropped (no resumption, PSK or 0-RTT). A CertificateEntry extension on any certificate
  (x509 reject `X509_ERR_ENTRY_EXT`, 21) is `TLS_ERR_EXTENSION` with detail 21, alert
  unsupported_extension. KeyUpdate is honoured, including update_requested.
- ChangeCipherSpec (stricter than RFC 8446 section 5): at most one, exactly the single byte 1, only
  after the ServerHello and before the first protected record. A second, early, late, protected
  or malformed CCS is `TLS_ERR_UNEXPECTED_MESSAGE`.
- Records: 64-bit sequence numbers, capped at 23726566 records per key (`TLS_ERR_SEQUENCE_LIMIT`,
  see "Key lifetime" above; the client updates its own key before that). A record over
  2^14 + 256 is refused. A protected record shorter than a tag plus one byte is
  `TLS_ERR_BAD_RECORD_MAC`; one whose TLSInnerPlaintext (content, type and padding) would exceed
  2^14 + 1 bytes is `TLS_ERR_RECORD`, alert record_overflow (RFC 8446 5.4). Plaintext is released
  only after its tag verifies. An end of TCP without
  close_notify is `TLS_ERR_EOF`, never a clean 0. A handshake message spread over more than 4000
  records is `TLS_ERR_MESSAGE_TOO_BIG`; more than 1000 consecutive records without application
  data in one `tls_read` (empty records, tickets, KeyUpdates) is `TLS_ERR_UNEXPECTED_MESSAGE`, and
  so is application data arriving while a post-handshake message is only partly received (RFC 8446
  5.1 forbids interleaving).
- A close_notify received during the handshake is `TLS_ERR_ALERT_RECEIVED` with detail 0.
- Not implemented: OCSP fetching and CRL fetching (stapled OCSP is checked with flag 8), Certificate Transparency, IPv6, DNSSEC, DNS over TCP,
  RFC 5280 name canonicalisation (issuer names are matched as exact DER bytes), and nameConstraints
  (always refused, see above). Path building commits to the first presented issuer whose
  signature verifies, without backtracking; this fails closed (availability only).
- DNS is unauthenticated (UDP to the first IPv4 nameserver in `/etc/resolv.conf`, random 16-bit
  ID, question echo checked). A spoofed answer can redirect the TCP connection but cannot pass
  server authentication. The answer must be a standard response (QR set, opcode 0, TC clear,
  RCODE 0); only A records whose owner is the queried name, or the target of a CNAME chain from
  it (case-insensitive compare), are used; a name over 127 labels or a looping pointer is
  `TLS_ERR_DNS`, never a trap.
- Caller-contract traps (not reachable by a peer): `tls_dial` with a hostname longer than the
  query buffer (validate it with `tls_init` first), `tls_write` of more than 16 GB at once,
  `/etc/resolv.conf` with more than 4096 consecutive blanks. A trust bundle larger than `TLSTSZ`
  fails closed.

## Side channels: what is and is not constant time

Constant time here means that no branch, loop count or memory index depends on a secret.

| unit | secret data | constant time? |
|---|---|---|
| `x25519.om` | scalar, shared secret | yes: fixed-count ladder, masked cswap, masked final reduction and zero check |
| `chacha.om` | key, keystream, Poly1305 state, tag | yes: ARX only; tag compare ORs all 16 bytes before deciding |
| `aesgcm.om` | key, round keys, H, data | yes: bitsliced AES (BearSSL `aes_ct64` layout, MIT, T. Pornin; `NOTICE.bearssl`), Boyar-Peralta depth-16 S-box circuit, masked GHASH; no table lookups |
| `gcmsiv.om` | key, derived keys, H, tag | yes, on the same terms as `aesgcm.om`: bitsliced AES and masked GHASH; tag compare ORs both 64-bit halves; branches only on lengths, key size and the tag-check outcome |
| `sha2.om`, `hkdf.om` | HMAC keys, PRKs, secrets | yes: branches only on lengths and the algorithm |
| `tls.om` | traffic secrets, keys, Finished | the key schedule, nonce and Finished compare (`tls_cteq`) are constant time. Branches depend on public values (suite, lengths, content type) |
| `ecdh.om` | private scalar, Z | yes: fixed-count Montgomery ladder (Renes-Costello-Batina complete formulas, masked swap), masked range check and final reduction; peer-point validation branches only on the public point |
| `ec.om`, `rsa.om`, `x509.om` | none (verification of public data) | no, by design. Do not reuse them for signing or ECDH |

Not covered, or covered only partly:

- **CPU timing.** omc emits plain AArch64. The constant-time claims rely on MUL, ADD, EOR, AND,
  ORR and the shifts having data-independent timing. PSTATE.DIT is not set: `tls_hwdit` reports
  whether the kernel advertises FEAT_DIT (HWCAP bit 24), and `msr DIT, #1` needs a compiler word
  that omc does not have yet (proposal in `THREAT_MODEL.md`).
- **Record padding.** The inner-plaintext padding is stripped by a backward scan, so its time
  reveals the padding length. RFC 8446 section 5.4 accepts this.
- **Lengths and outcomes.** Message lengths, record counts and whether a tag check failed are
  visible, as in every TLS.
- **Wiping.** Secrets in registers, spilled stack slots and caller-owned buffers are not scrubbed
  by the compiler. The units zero their own secret local arrays, and `tls_close` and failures
  zero the keys in `c`.
- **Conversions.** A `uchar` conversion of a secret value compiles to a range check whose branch
  is never taken.
- **Measurement.** A dudect-style fixed-vs-random Welch t-test (`test/tls/timing.om`: classes
  balanced and randomly interleaved, identical preparation for both classes; 100000 to 500000
  samples per run, 3 runs per operation on core 17 (Cortex-X925) and core 2 (Cortex-A725), full data
  plus 12 percentile crops, t from Wolfram), 2026-10-26: no |t| above 4.5 for x25519,
  ChaCha20-Poly1305 seal, AES-128-GCM seal and HKDF-SHA256 (max 3.52). `tls_cteq` was flagged
  intermittently on core 17 (5 of 15 runs, |t| up to 27.97 in cropped tests, both signs, full-data
  |t| at most 2.58) and not on cores 2 or 9; its machine code has no secret-dependent branch or
  address, and the cause is not established. MEASURED on one busy host (load 16-28 on 20 cores):
  a screen, not a proof. Evidence: `evidence/om-tls-review-2026-10-26/timing-v3/README.md`. The
  x25519 figures in its test output are informational.
  P-256 `ecdh_shared` (`timing ecdh`, 20000 samples on cpu0 and cpu5, scalar fixed vs random, v2
  driver): every |t| below 0.8, full and cropped at the pooled 90th percentile, 2026-10-26, MEASURED
  on one loaded host (`/home/drakestapleton/handoffs/TlsProof/ApprovedMode/timing/welch.md`); P-384
  NOT_RUN.

## Tests (run from the repository root)

| command | final line observed 2026-10-26 (omc `2c824805`; identical on `189ada82`; `net=PASS` observed 2026-10-26 on both) |
|---|---|
| `sh om/tls/test/sha2/run.sh OMC` | `SHA2 PASS vectors=1777 (wycheproof=774) mutants=5/5` |
| `sh om/tls/test/x25519/run.sh OMC` | `X25519 PASS vectors=531 (wycheproof=518) mutants=7/7` |
| `sh om/tls/test/chacha/run.sh OMC` | `CHACHA PASS vectors=3087 (wycheproof=339) mutants=5/5` |
| `sh om/tls/test/aesgcm/run.sh OMC` | `AESGCM PASS vectors=47570 (fips197=4, nist=47250, wycheproof=316) mutants=5/5` |
| `sh om/tls/test/gcmsiv/run.sh OMC` | `GCMSIV PASS vectors=51 (rfc8452 c1=24 c2=24 c3=2 s8=1) checks=422 mutants=6/6` (2026-10-30, omc `62775d2a`) |
| `sh om/tls/test/reuse/run.sh OMC` | `REUSE PASS checks=27 mutants=6/6` (2026-10-30, omc `62775d2a`) |
| `sh om/tls/test/ec/run.sh OMC` | `EC PASS vectors=3984 (wycheproof=3748 cavp=180 edge=56) mutants=6/6` |
| `sh om/tls/test/ecdh/run.sh OMC` | `ECDH PASS vectors=2458 (cavp=100 wycheproof=2284 edge=74) mutants=9/9` (omc `2c824805`, 2026-10-26) |
| `sh om/tls/test/rsa/run.sh OMC` | `RSA PASS vectors=6486 (wycheproof=5250) mutants=4/4` |
| `sh om/tls/test/x509/run.sh OMC` | `X509 PASS cases=91 (openssl-parity=73/91) mutants=12/12` |
| `sh om/tls/test/drbg/run.sh OMC` | `DRBG PASS vectors=810 (acvp=90, cavp=720) api=23 mutants=13/13` (observed 2026-10-26, omc `2c824805`) |
| `sh om/tls/test/fips/run.sh OMC` | `FIPS PASS checks=49 drbgerr=50 ecgenerr=52 kat-mutants=14/14 code-mutants=18/18` (observed 2026-10-26, omc `2c824805`, with the approved-only ECDH key generation; same line 2026-10-27 with the integrity test, detail 16) |
| `sh om/tls/test/integrity/run.sh OMC [OUT]` | `INTEGRITY PASS checks=14 mutants=4` (observed 2026-10-27, omc `2c824805`) |
| `sh om/tls/test/fips/entropy.sh OMC EA_CPP_DIR` | `ENTROPY PASS ...` with the min-entropy estimates; needs the NIST SP800-90B_EntropyAssessment tool built separately |
| `TLS_NET=1 sh om/tls/test/tls/run.sh OMC` | @@GATE-TLS@@ Before the integration: `interop=44 ... mutants=27/27` on audit/tls-suites and `interop=57 ... mutants=34/34` on audit/tls-fips-approved (omc `2c824805`; `net=NOT_RUN` without `TLS_NET`) |
| `sh om/tls/build.sh OMC om/tls/test/tls/timing.om build/tls/timing` then `nice -n 19 taskset -c CPU build/tls/timing WHAT N` | dudect samples (`CLASS NANOSECONDS` lines), statistics outside; see `THREAT_MODEL.md` |

How the suites are built:

- **Wycheproof.** Vectors come from C2SP/wycheproof `d17cc89fa5c8f4f4d378b5385ebf75da5d0926c9`,
  and every test of each in-scope file is run. Each suite prints the files it does not run, and
  why, as NOT_RUN: SHA-1, SHA-224, SHA-3, SHAKE, other curves, XChaCha20, RSA
  encryption. AES-GCM-SIV is `gcmsiv.om`; wycheproof `aes_gcm_siv_test.json` is not vendored. Wycheproof has no X.509 path vectors.
- **Other vectors.** NIST CAVP SHAVS, GCM and ECDSA SigVer; RFC 7748, 8439, 4231, 5869 and 8448.
- **Mutants.** Each suite has library mutants that must be caught as MISMATCH (exit 1), plus an
  expectation mutant and flip-a-byte negative controls. In the x509 and DNS mutants a runtime trap
  (exit >= 100) also counts as detected, because those mutants remove a bound that keeps hostile
  input from trapping.
- **`test/tls`.**
  - The RFC 8448 section 3 KAT checks ECDHE, every secret, the keys and IVs, both Finished values,
    and record seal and open byte for byte.
  - `interop.sh` runs 44 rows against `openssl s_server`, crafted ServerHellos and `fakesrv.py`
    (a full TLS 1.3 server in Python that misbehaves after a valid handshake), all on 127.0.0.1.
    Ports already in use are skipped. Every failure row also checks that `tls_close` leaves the
    connection failed.
  - `dnsunit.om` runs the 23 cases of `vectors/dns.txt` (`gen_dns.py`): 21 DNS answers and two
    host names over 253 bytes that `dns_a4` must refuse before building its query.
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
| crafted ServerHello: TLS 1.2, HRR with a ServerHello-shaped key_share, AES-256 suite, session id, P-256 share, zero share, duplicate extension, trailing byte | `PROTOCOL_VERSION`, `DECODE`, `CIPHER_SUITE`, `SESSION_ID_ECHO`, `KEY_SHARE`, `ZERO_SHARED_SECRET`, `EXTENSION`, `DECODE` |
| openssl s_server `-groups X25519`, client withholds its x25519 share (FLAGS 2, hybrid share only): real HelloRetryRequest | accepted |
| X25519MLKEM768, `fakesrv.py` with kyber-py 1.2.0 as the ML-KEM oracle: hybrid handshake (default and FLAGS 2); server picks x25519 (`hybx`); share one byte short; zero x25519 half; one ciphertext bit flipped (implicit rejection) | accepted, accepted, `KEY_SHARE`, `ZERO_SHARED_SECRET`, `BAD_RECORD_MAC` |
| X25519MLKEM768, OpenSSL 3.5.4 `s_server -trace` (`OPENSSL35`, `openssl35.sh`; the group of the server's key_share is checked): hybrid-only, default groups, FLAGS 2 hybrid-only, FLAGS 2 x25519-only (HRR), x25519-only, approved mode (P-256 chosen), pure MLKEM768-only server | accepted ×6, `ALERT_RECEIVED` detail 40 |
| fake HRR (FLAGS 2): with and without cookie, with CCS after it (`hrrok`, `hrrnock`, `hrrccs`); the server checks ClientHello2's random, session id, single share and cookie echo | accepted |
| fake HRR: client had already shared x25519; group never offered; second HRR; suite changed in ServerHello; no change requested; early_data extension; unknown extension 0x0a0a; no supported_versions; 12001-byte cookie | `KEY_SHARE`, `KEY_SHARE`, `UNEXPECTED_MESSAGE`, `CIPHER_SUITE`, `HELLO_RETRY`, `ILLEGAL_PARAMETER`, `EXTENSION`, `PROTOCOL_VERSION`, `MESSAGE_TOO_BIG` |
| NewSessionTicket: well formed; lifetime 604801; empty ticket; trailing byte; extension past the block | accepted, `TICKET`, `DECODE`, `DECODE`, `DECODE` |
| leaf CertificateEntry with a status_request extension (`entryext`) | `TLS_ERR_EXTENSION` detail 21 |
| `tls_read` into an empty buffer, then the normal read (`ok`, FLAGS 4) | -`TLS_ERR_ARGUMENT`, then accepted |
| `seed/net/https.om` `htlssend` past the end of `io`, then all 3548320 bytes of a 3.5 MiB `io` (`bigsend.om` to `sink`, every byte checked) | `HEINVAL` (no trap), then accepted in 217 records |
| one bit flipped in a server record by a proxy | `TLS_ERR_BAD_RECORD_MAC` |
| fake server (`fakesrv.py`), valid handshake; 50 empty application records before the data | accepted |
| second CCS, 5000 CCS, CCS before ServerHello, CCS after the first protected record, CCS inside a protected record | `TLS_ERR_UNEXPECTED_MESSAGE` |
| a 6000-byte Certificate sent one byte per record | `TLS_ERR_MESSAGE_TOO_BIG` |
| CertificateVerify signature flipped; signed with a key that is not the leaf's | `TLS_ERR_CERTIFICATE_VERIFY` with `X509_ERR_BAD_SIGNATURE` |
| Certificate then Finished, no CertificateVerify | `TLS_ERR_UNEXPECTED_MESSAGE` |
| 1500 empty application records; 1500 NewSessionTickets | `TLS_ERR_UNEXPECTED_MESSAGE` |
| close_notify before the ServerHello and after it | `TLS_ERR_ALERT_RECEIVED` detail 0 |
| record limit lowered to 5: server sends exactly 4 records per key then KeyUpdate (`kuexact`), rotates twice (`kurot`) | accepted |
| record limit lowered to 5: server sends a 5th record under one key (`kuover`) | `TLS_ERR_SEQUENCE_LIMIT` |
| record limit lowered to 8, client writes 8 single-byte records: the server (`kuw:8`) alerts unless a KeyUpdate arrives in time | accepted (13 updates seen) |
| ServerHello one byte per 250 ms for 8 s, handshake budget 2 s (`loris`) | `TLS_ERR_TIMEOUT` |
| ServerHello one byte per 20 ms, budget 10 s (`slowsh`) | accepted |

### Differences from `openssl verify`

The x509 suite runs `openssl verify -x509_strict -purpose sslserver -verify_hostname HOST -attime
NOW` (OpenSSL 3.0) on every chain case and fails on any difference that
`test/x509/vectors/openssl-diff.txt` does not list, and on any listed case where OpenSSL agrees.
18 of the 91 cases differ. In every one this client refuses and OpenSSL accepts: there is no case
where this client accepts a chain OpenSSL refuses. The two such cases the review found
(`store_intermediate_anchor`, `at_not_after`) were closed by making this client stricter (above);
mutants anchor-self-issued and date-at-not-after reopen them and are caught.

| case | this client | why it refuses |
|---|---|---|
| `wildcard_partial` | `NAME_MISMATCH` | partial-label wildcard `f*.wild.test` (RFC 6125 6.4.3 SHOULD NOT) |
| `no_san` | `NO_SAN` | no subject CN fallback (RFC 6125 6.4.4, CA/B BR require SAN) |
| `name_constraints_critical` | `NAME_CONSTRAINTS` | nameConstraints not implemented, so it is refused (finding 5) |
| `name_constraints_noncritical` | `NAME_CONSTRAINTS` | same, also when not critical: ignoring it would accept names the CA was constrained against (finding 5) |
| `depth_8_intermediates` | `TOO_DEEP` | at most 8 non-anchor certificates; OpenSSL allows depth 100 |
| `leaf_is_ca` | `LEAF_IS_CA` | a cA TRUE certificate is not a server leaf |
| `leaf_ku_no_digsig` | `KEY_USAGE` | TLS 1.3 signs with the leaf key, so keyUsage must include digitalSignature |
| `rsa_1024_leaf` | `WEAK_KEY` | RSA below 2048 bits; OpenSSL security level 1 accepts 1024 |
| `sha1_signed` | `UNSUPPORTED_ALG` | only SHA-256/384/512 signatures |
| `rsassa_pss_key` | `UNSUPPORTED_ALG` | only rsaEncryption and P-256/P-384 keys; id-RSASSA-PSS keys refused |
| `ed25519_key` | `UNSUPPORTED_ALG` | no Ed25519 in om/tls |
| `leaf_too_big` | `MALFORMED` | leaf over 16896 bytes refused before parsing (finding 2) |
| `rsa_6144_leaf` | `WEAK_KEY` | presented RSA moduli above 4096 bits (finding 6, bounds verification cost) |
| `rsa_6144_intermediate` | `WEAK_KEY` | same policy for presented intermediates |
| `rsa_e_2_32_plus_1_leaf` | `WEAK_KEY` | presented exponent outside 3 .. 2^32-1 (finding 6) |
| `rsa_e_2_32_plus_1_intermediate` | `WEAK_KEY` | same policy for presented intermediates |
| `rsa_e_even_leaf` | `WEAK_KEY` | even exponent 65536; OpenSSL verify does not check the leaf exponent |
| `rsa_e_1_leaf` | `WEAK_KEY` | exponent 1; OpenSSL verify does not check the leaf exponent |

## Security review

The security review of omega-mv #813 (`~/handoffs/omega-tls/security-review.md`, head `de2d884b`)
had 11 findings. Each one is fixed and has a test that fails on the code before the fix.

| finding | fix | test |
|---|---|---|
| F1 repeated uniqueIDs trap `x509_tbs2` | issuerUniqueID then subjectUniqueID, each at most once, else `X509_ERR_MALFORMED` (`x509.om` `x509_tbs2`) | x509 P `uid_*` cases, mutant unique-id-order |
| F2 element bounds reachable | each element loop stops at 9999 (`X5NMAX`) with `X509_ERR_MALFORMED`; a leaf over 16896 bytes (`X5LMAX`) is refused in `x509_chain_verify` | x509 P eku/san/ext cases, C `leaf_too_big`, mutants leaf-size and elemcap |
| F3 `tls_hsmsg` traps on empty records | more than 4000 records for one message is `TLS_ERR_MESSAGE_TOO_BIG` | interop `frag`, mutant handshake record cap |
| F4 `tls_read` traps on empty records | more than 1000 records without data is `TLS_ERR_UNEXPECTED_MESSAGE` (`tls_abort`) | interop `empty1500`, `tickets1500`, `empty50` (accepted), mutant read record cap |
| F5 non-critical nameConstraints ignored | any nameConstraints is `X509_ERR_NAME_CONSTRAINTS` (code 20) in `x509_ext` | x509 C `name_constraints_*`, E row 20, mutant name-constraints-noncritical |
| F6 attacker-chosen RSA keys cost CPU | presented keys need 2048-4096 bits and an odd exponent in 3 .. 2^32-1 (`x509_rsaok`, `chain_rsapol`, `chain_leaf`, `x509_cv_verify`) | x509 C `rsa_*`, V `cv_rsa_6144`, `cv_rsa_e_2_32_plus_1`, mutants rsa-modulus-max, rsa-exponent |
| F7 protected and repeated CCS accepted | CCS policy above (`TLCCS` state in `tls_rdrec`; inner type 20 refused) | interop `ccs2`, `ccsflood`, `ccspre`, `ccslate`, `encccs`, mutant CCS count |
| F8 DNS answer handling | header flags checked, owner names compared case-insensitively along the CNAME chain, label count and pointer steps capped, no trap (`dns_parse`, `dns_ans`, `dns_skip`, `dns_neq`) | `dnsunit.om` (21 cases), DNS mutants |
| F9 no CertificateVerify negative control | fake server with a flipped signature and with a foreign key | interop `cvbad`, `cvkey`, mutant CertificateVerify ignored |
| F10 F1-F4 and F7 not exercised | crafted x509 fixtures (`gen_crafted.py`), fake-server rows, DNS unit; a trap is a FAIL in interop | all rows above |
| F11 info items | `tls_close` keeps a failed connection failed; close_notify in the handshake is `TLS_ERR_ALERT_RECEIVED`; deadline, clock, public-suffix, caller-contract, CertificateRequest and path-building limits documented above | interop `CLOSE_NOT_FAILED` check on every failure row, `closehs`, `closepre`, mutants close revives failed and handshake close_notify unmapped |

MODEL-REVIEWED (2026-10-26, branch `audit/tls-outside-review`; a code-reading review by our own review
agents, not an outside review): six gaps, each fixed here or stated as a
limit in `THREAT_MODEL.md`; the audit boundary is `AUDIT_SCOPE.md`. In short: key lifetime
(KeyUpdate at the record limit, `TLS_ERR_SEQUENCE_LIMIT` fail-closed), HTTPS transport for
`seed/net/http.om`, a hosted arm64 CI job (`.github/workflows/om-tls-arm64.yml`), the handshake
budget, secret wiping checked by `test/tls/residue.py` (a `/proc/PID/mem` scan after the handshake and
after `tls_close`, with three mutants), the DIT probe, the timing measurement, and the review bundle
retained under `evidence/om-tls-review-2026-10-26/` with SHA-256 sums.

## Policy

- **Timeouts.** A caller deadline (`tls_deadline`) bounds every call; without one the handshake is
  bounded by `tls_hslimit` (default 30 s) and reads block as the socket does. `seed/net/https.om`
  maps a TLS timeout to `HETIME`.
- **Key lifetime.** 23726566 records per direction and key (RFC 8446 section 5.5, the stricter
  ChaCha20-Poly1305 figure for both suites); the client sends KeyUpdate one record before and refuses
  with `TLS_ERR_SEQUENCE_LIMIT` at the limit on both directions.
- **Secret handling.** Every unit zeroes its own secret locals; `tls_close` and every failure zero
  the keys, the ephemeral key, the handshake secrets and the record buffers in `c`. Registers and
  compiler-spilled stack slots are not scrubbed (a compiler limit). `test/tls/residue.py` is the
  check: after the handshake none of the twelve handshake-only secrets is in the process memory,
  after `tls_close` none of the eighteen.
- **Revocation (opt-in, `tls_init` flag 8, `rev.om`).** With flag 8 the client sends
  `status_request`, checks the leaf's stapled OCSP response (signature by the issuer or a delegated
  id-kp-OCSPSigning responder, CertID, thisUpdate within 300 s, nextUpdate required) and refuses
  revoked, unknown and stale answers; a must-staple leaf (RFC 7633) without a staple is refused. A
  plain leaf without a staple is accepted (no OCSP fetch). Failures are `TLS_ERR_CERTIFICATE` with
  the rev.om code (32..38, `rev_errname`) as detail. `rev_crl` checks a caller-supplied CRL; the
  client fetches none. Without flag 8 revocation is not checked and a revoked chain verifies.
  Tests: `test/rev/run.sh` (unit, openssl oracle, fuzz, mutants) and the `rev:` rows of interop.sh.
- **Trust.** Only the caller's trust store (`tls_storef`); no system store, no name constraints,
  no CT, as `What it does and refuses` says.

The read-only re-review of the fix (`~/handoffs/omega-tls/security-rereview.md`, `38c80a85`) found
every finding fixed (VERDICT: PASS) and two residual items, both fixed here: N1, a caller host name
of 288 bytes or more trapped `dns_a4` (now refused above 253 bytes; `dnsunit` L cases, mutant
dns_a4 name length unchecked), and N2, no row for a server that omits CertificateVerify (interop
`cvskip`).

## Writing another unit for this set

- Every function name carries its unit prefix and a `_`.
- Constants and texts are UPPERCASE with a two-letter unit code.
- Locals are at most 8 characters and must not collide with any composed name or host word.
- A function takes at most 6 parameter slots; an unsized array uses 2.
- Error 12 means the function needs more than 16 registers: split it.
- Untrusted input must never trap. Check every length before indexing.
