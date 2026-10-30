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
| `x509.om` | strict DER, X.509 v3, PEM trust store, path validation, CertificateVerify | 70 |
| `tls.om` | handshake, record layer, key schedule, alerts, DNS A lookup, TCP connect | 105 |

The total is 340 functions. omc at `2c824805` (the batch 2 pin, `OMC_PIN` in `om/units.sh`, which
the ec, rsa and x509 suites check) allows 4095 functions per unit, as `189ada82` did.

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
tls_deadline c monotonic_ns              absolute deadline (monons) for every wait; 0 = 30 s per wait,
                                         with no cap on the whole handshake (a slow server can stretch
                                         it; CPU time is never covered)
e  = tls_connect c st int fd             full handshake; any failure sends an alert and is final
n  = tls_read c out                      > 0 bytes; 0 = close_notify received; -code on error
n  = tls_write c bytes                   all bytes or -code
tls_close c                              send close_notify, wipe keys (the caller closes fd); a failed
                                         connection stays failed, so a later tls_read is -code, never 0
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
  extensions are refused. ALPN must be the one protocol offered.
- A CertificateRequest is answered with an empty Certificate; there are no client certificates.
  Its extensions are not validated (the empty Certificate makes them irrelevant).
  NewSessionTicket is ignored (no resumption, PSK or 0-RTT). KeyUpdate is honoured, including
  update_requested.
- ChangeCipherSpec (stricter than RFC 8446 section 5): at most one, exactly the single byte 1, only
  after the ServerHello and before the first protected record. A second, early, late, protected
  or malformed CCS is `TLS_ERR_UNEXPECTED_MESSAGE`.
- Records: 64-bit sequence numbers, never wrapped (`TLS_ERR_SEQUENCE_LIMIT`). A record over
  2^14 + 256 is refused. Plaintext is released only after its tag verifies. An end of TCP without
  close_notify is `TLS_ERR_EOF`, never a clean 0. A handshake message spread over more than 4000
  records is `TLS_ERR_MESSAGE_TOO_BIG`; more than 1000 consecutive records without application
  data in one `tls_read` (empty records, tickets, KeyUpdates) is `TLS_ERR_UNEXPECTED_MESSAGE`.
- A close_notify received during the handshake is `TLS_ERR_ALERT_RECEIVED` with detail 0.
- Not implemented: revocation (OCSP, CRL), Certificate Transparency, IPv6, DNSSEC, DNS over TCP,
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

| command | final line observed 2026-10-26 (omc `189ada82`; identical on `2c824805`, including `net=PASS`) |
|---|---|
| `sh om/tls/test/sha2/run.sh OMC` | `SHA2 PASS vectors=1777 (wycheproof=774) mutants=5/5` |
| `sh om/tls/test/x25519/run.sh OMC` | `X25519 PASS vectors=531 (wycheproof=518) mutants=7/7` |
| `sh om/tls/test/chacha/run.sh OMC` | `CHACHA PASS vectors=3087 (wycheproof=339) mutants=5/5` |
| `sh om/tls/test/aesgcm/run.sh OMC` | `AESGCM PASS vectors=47570 (fips197=4, nist=47250, wycheproof=316) mutants=5/5` |
| `sh om/tls/test/ec/run.sh OMC` | `EC PASS vectors=3984 (wycheproof=3748 cavp=180 edge=56) mutants=6/6` |
| `sh om/tls/test/rsa/run.sh OMC` | `RSA PASS vectors=6486 (wycheproof=5250) mutants=4/4` |
| `sh om/tls/test/x509/run.sh OMC` | `X509 PASS cases=91 (openssl-parity=73/91) mutants=12/12` |
| `TLS_NET=1 sh om/tls/test/tls/run.sh OMC` | `TLS PASS vectors=24 (rfc8448) dns=23 interop=38 net=PASS mutants=19/19` |

How the suites are built:

- **Wycheproof.** Vectors come from C2SP/wycheproof `d17cc89fa5c8f4f4d378b5385ebf75da5d0926c9`,
  and every test of each in-scope file is run. Each suite prints the files it does not run, and
  why, as NOT_RUN: SHA-1, SHA-224, SHA-3, SHAKE, other curves, XChaCha20, AES-GCM-SIV, RSA
  encryption. Wycheproof has no X.509 path vectors.
- **Other vectors.** NIST CAVP SHAVS, GCM and ECDSA SigVer; RFC 7748, 8439, 4231, 5869 and 8448.
- **Mutants.** Each suite has library mutants that must be caught as MISMATCH (exit 1), plus an
  expectation mutant and flip-a-byte negative controls. In the x509 and DNS mutants a runtime trap
  (exit >= 100) also counts as detected, because those mutants remove a bound that keeps hostile
  input from trapping.
- **`test/tls`.**
  - The RFC 8448 section 3 KAT checks ECDHE, every secret, the keys and IVs, both Finished values,
    and record seal and open byte for byte.
  - `interop.sh` runs 38 rows against `openssl s_server`, crafted ServerHellos and `fakesrv.py`
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
| crafted ServerHello: TLS 1.2, HRR, AES-256 suite, session id, P-256 share, zero share, duplicate extension, trailing byte | `PROTOCOL_VERSION`, `HELLO_RETRY_UNSUPPORTED`, `CIPHER_SUITE`, `SESSION_ID_ECHO`, `KEY_SHARE`, `ZERO_SHARED_SECRET`, `EXTENSION`, `DECODE` |
| one bit flipped in a server record by a proxy | `TLS_ERR_BAD_RECORD_MAC` |
| fake server (`fakesrv.py`), valid handshake; 50 empty application records before the data | accepted |
| second CCS, 5000 CCS, CCS before ServerHello, CCS after the first protected record, CCS inside a protected record | `TLS_ERR_UNEXPECTED_MESSAGE` |
| a 6000-byte Certificate sent one byte per record | `TLS_ERR_MESSAGE_TOO_BIG` |
| CertificateVerify signature flipped; signed with a key that is not the leaf's | `TLS_ERR_CERTIFICATE_VERIFY` with `X509_ERR_BAD_SIGNATURE` |
| Certificate then Finished, no CertificateVerify | `TLS_ERR_UNEXPECTED_MESSAGE` |
| 1500 empty application records; 1500 NewSessionTickets | `TLS_ERR_UNEXPECTED_MESSAGE` |
| close_notify before the ServerHello and after it | `TLS_ERR_ALERT_RECEIVED` detail 0 |

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
