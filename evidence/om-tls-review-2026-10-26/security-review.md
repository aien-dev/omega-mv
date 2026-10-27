# Security review: om/tls (aien-dev/omega-mv, branch feat/om-tls, tree /tmp/omtls)

Reviewer: security-reviewer subagent, read-only. **Nothing was compiled or run.** Every runtime
statement below comes from reading the code and the OMEGESE trap rules (`docs/one_layer/OMEGESE.md:58-60,131-134`:
a `while … bound N` traps 4 if its condition is still true at the head of pass N+1; a trap ends the process with
exit 100+code). Runtime status is NOT_RUN: no finding was EXERCISED.
Contract: `/home/drakestapleton/handoffs/omega-tls/tls-contract.md` ("untrusted input must never trap",
"secrets constant time", "tag compare constant time").

Line numbers refer to the tree as read on 2026-10-26 (tls.om 2130 lines, x509.om 1955 lines).

## Findings

### F1. MEDIUM: certificate with 5 uniqueIDs traps the client before authentication. x509.om:1004
`x509_tbs2` reads issuerUniqueID/subjectUniqueID with `while lt s b bound 4`. Each `0x81`/`0x82` element
`continue`s, and nothing enforces order or uniqueness.
- Failing input: a v2 or v3 TBSCertificate whose subjectPublicKeyInfo is followed by `81 00 81 00 81 00 81 00 81 00`,
  or by four `81 00` and then the `[3]` extensions. Pass 5's head sees `s < b` and the process traps 4 (exit 104).
- Reach: the leaf is parsed in `chain_leaf`, and presented intermediates in `chain_try`, before any signature is
  checked. Any server can kill the client. So can an on-path or DNS attacker, because the Certificate comes from
  whoever terminates the handshake.
- Violates the contract rule "untrusted input must never trap", and README.md:165.
- Fix: accept at most one `0x81`, then at most one `0x82`, in that order (track the last tag), otherwise
  X5EMAL; or replace the bound with a counter that returns X5EMAL.

### F2. MEDIUM: element-count bounds of 10000 are reachable from a single certificate (trap 4). x509.om:809, 845, 930, 1534
`x509_eku` (809), `x509_san` (845), `x509_exts` (930) and `chain_host` (1534) each loop
`while lt s b bound 10000`, one element per pass.
- Failing inputs (smallest elements):
  - EKU containing 10001 × `06 00`: about 20 KB.
  - SAN containing 10001 × `80 00` (an otherName-tag-shaped GeneralName, skipped as non-dNSName): about 20 KB.
    This traps in `x509_san`, or in `chain_host` if parsing passes.
  - Extensions containing 10001 × `30 04 06 00 04 00`: about 60 KB. The duplicate check needs a known OID, so
    empty or unknown OIDs repeat freely; if empty OIDs are refused, use `30 05 06 01 00 04 00`, 7 bytes, 70 KB,
    which still fits.
- Reach: the leaf has no size cap before `x509_chain_verify`; TLLFSZ is only checked in `tls_leaf`, after
  verification (tls.om:1240-1250). A Certificate message can be up to TLHBSZ = 65536 bytes. Presented
  intermediates are capped at X5KBUF = 16384 bytes, which is below the EKU/SAN threshold, so the leaf is the
  vector. Same attacker set as F1.
- Fix: return X5EMAL when a counter reaches the cap instead of relying on `bound`. Alternatively set each bound
  above the largest count the byte budget allows (len/2 + 1 for 2-byte elements). Cheapest of all: cap leaf
  size at TLLFSZ before calling `x509_chain_verify`, then make each bound ≥ 16896/2.

### F3. MEDIUM: `tls_hsmsg` traps on 4096 records that add no handshake bytes (on-path, before authentication). tls.om:588
`tls_hsmsg` is `while true bound 4096`, one record per pass. `tls_rdrec` (tls.om:527-532) accepts an
unencrypted ChangeCipherSpec record (`14 03 03 00 01 01`) at any time, including after handshake keys are
installed, because the type-20 test runs before the `TLRK` test. `tls_hsmsg` then skips it without counting.
- Failing input: an on-path attacker injects 4096 six-byte CCS records (24 KB) into the server's flight after
  ServerHello. The client traps 4. With an error instead of a trap, the same attacker would only cause an
  ordinary connection failure.
- Second trigger: a hostile but authenticated-later server fragments EncryptedExtensions/Certificate into ≥ 4096
  one-byte records. The record layer is legal and Certificate may be 65 KB.
- `ret TLEUNEX` at the end of the loop (≈ line 619) can never run.
- Fix: allow at most one plaintext CCS (RFC 8446 §5 middlebox compatibility), and none after the first protected
  record. Count records in a variable and return TLEUNEX/TLEBIG at a cap at or above TLHBSZ + 8, rather than
  reaching `bound`.

### F4. LOW-MEDIUM: `tls_read` traps on 999,999 records that carry no data (authenticated peer). tls.om:1714
`while true bound 999999` only exits when `TLPN > 0`, at close_notify, or on an error. Zero-length
application_data records are valid. Each is 22 bytes on the wire: header, inner type, tag.
NewSessionTicket-only records (`tls_post` → `tls_hsdrop`) and KeyUpdate also leave `TLPN = 0`.
- Failing input: the authenticated server (or a compromised CDN edge) sends 999,999 empty type-23 records
  (≈ 22 MB). The client traps 4 inside `tls_read`.
- Fix: count consecutive records that deliver no data and return a normal error (for example TLEUNEX after
  1024), or turn the loop into an explicit counter that ends in an error.

### F5. MEDIUM: a non-critical nameConstraints extension is silently ignored. x509.om `x509_ext` (≈861-917), README.md:79-80
Only a *critical* nameConstraints (known-OID index 20) returns X5ECRIT. A non-critical one is parsed past
and its restrictions are never applied.
- Scenario: a technically constrained subordinate CA, for example `permitted;DNS:example.com`, marked
  non-critical. Historically the CA/B Forum Baseline Requirements and Mozilla allowed this for compatibility.
  If that CA issues for `api.github.com` or `mail.gandi.net`, this client accepts the chain. OpenSSL and NSS
  enforce the constraint and refuse it.
- The README documents the behaviour ("a critical one is refused"), but it is a fail-open default on a path
  that protects the GitHub token and SMTP credentials. The only test is `name_constraints_critical`
  (gen_pki.sh intNC, `critical,permitted;DNS:example.test`), so no non-critical case is covered.
- Fix: refuse nameConstraints on any issuer whether critical or not (X5ECRIT), or implement dNSName
  permitted/excluded subtree matching for the leaf SAN. Add a non-critical test case either way.

### F6. LOW: attacker-chosen RSA exponents give a large CPU cost per presented certificate. rsa.om:16-17, 28-29 (policy), `rsa_key` (340-389); x509.om:1568-1581 (`chain_sig`)
`rsa_key` accepts any odd e ≥ 3 with bitlen(e) ≤ bitlen(n), and n up to 8192 bits. The rsa.om note says the
cost "is a CPU cost the caller accepts by passing such a key". x509.om's `chain_sig` passes keys taken from
*presented* candidate certificates, which the attacker controls, and limits only the modulus minimum (≥ 2048).
- Modelled cost: one Montgomery product for s = 256 limbs is about 2s² = 131,072 limb mul-adds. An all-ones
  8192-bit e needs about 8192 squarings and 8192 multiplies, about 2.1 × 10⁹ limb mul-adds per verification.
  For e = 65537 with a 2048-bit key it is 2·32²·18 ≈ 36,864.
  `WolframLanguageEvaluator: s=256; {2 s^2, 2 s^2*16384, 2*32^2*18} -> {131072, 2147483648, 36864}`.
- Up to 15 presented candidates can be tried, and none of this time counts against `tls_deadline`, which only
  covers I/O waits. A MITM can hold the client CPU-bound for a long time. Wall-clock time is not measured.
- Fix: at the X.509 layer, require e ≤ 2³² − 1 (or exactly 65537) and n ≤ 4096 bits for keys in presented
  certificates and the leaf. Web PKI root programs already require these limits.

### F7. LOW: protected and repeated ChangeCipherSpec records are accepted during the handshake. tls.om:527-532, 609-616
Two defects share one root cause:
1. A record whose *inner* content type is 20 after decryption is skipped silently in `tls_hsmsg`, because the
   test is `if ne ty 20`. RFC 8446 §5 requires the unexpected_message alert for an encrypted CCS.
2. Plaintext CCS is accepted any number of times, and also before ServerHello.

The fix in F3 covers both: accept CCS only as an unprotected record, at most once, between ServerHello and the
server Finished.

### F8. LOW-MEDIUM (DoS) / INFO: DNS answer handling. tls.om:1886-1890 (comment), 1930 (`dns_skip`), 1950-2000 (`dns_parse`)
- `dns_skip` is `while lt p z bound 128`. An answer owner name made of ≥ 128 one-byte labels (256 bytes) traps 4.
  Attackers able to send this: the resolver, an on-path attacker, or an off-path spoofer that matches the 16-bit
  ID on the connected UDP port. Fix: return 0 after 127 labels, or when the name passes 255 bytes.
- The flag mask 33295 (0x820F) at line 1959 tests QR, TC and RCODE, but not the opcode (bits 11-14). This
  contradicts the comment at line 1789 ("opcode 0"). Fix: mask 0xFA0F, expect 0x8000.
- The owner name of the answer RR is never compared with the question. The first IN A record anywhere in the
  answer section is used.
- One `recv` per attempt, so a single garbage datagram fails the lookup.
- TLS authentication limits all of this to denial of service, as README.md:81-83 says.

### F9. LOW (test gap): no negative control for CertificateVerify. test/tls/run.sh, interop.sh, kat.om
- No interop row and no KAT check reaches `TLECV`, and kat.om has no CV or x509 reference at all.
- In every row, the chain is either valid with an honest CertificateVerify, or rejected earlier at the chain
  check.
- A mutant `s/^ret TLECV$/ret 0/` (tls.om:1278) or "ignore `tls_cvcall`" would survive the whole suite
  [INFERENCE from reading the rows; not executed].
- CertificateVerify is the only proof that the server holds the leaf key. Without it, an attacker who replays a
  valid public certificate chain is stopped only by the Finished MAC, which the same attacker can compute
  because it chose the ECDHE share.
- Fix:
  - Add a fakesh-style server that sends a valid chain for `localhost` (from the test PKI) plus a CV signed by a
    different key, expecting TLS_ERR_CERTIFICATE_VERIFY (alert 51).
  - Add a server that skips CV (Certificate followed directly by Finished), expecting TLEUNEX.
  - Add both as `imut` mutants.

### F10. LOW (test gap): fuzzing and interop do not exercise F1-F4 or F7. test/x509/vectors/cases.txt:181-184, test/tls/fakesh.py
- The x509 fuzz applies 800 deterministic single mutations to valid lists. Those never repeat an element 5 or
  10001 times.
- fakesh only crafts the ServerHello.
- No case covers CCS flooding, inner type 20, record fragmentation, zero-length application data, KeyUpdate,
  NewSessionTicket floods, truncation before close_notify, or CertificateRequest.
- Add each as an interop or fakesh row that expects a named error and treats any exit ≥ 100 (trap) as FAIL.

### F11. INFO
- `tls_close` (tls.om:1772-1782) sets TLPH = 2 even from state 3 (failed). A later `tls_read` then returns 0,
  which reads like a clean close_notify. Keep state 3 when the connection had failed.
- A close_notify received during the handshake makes `tls_connect` return internal code TLCLOSE (100), which
  `tls_errname` maps to TLS_ERR_UNKNOWN. The return is nonzero, so this is not a bypass, but it should be a
  named error.
- `tls_deadline` 0 means 30 s per wait with no overall cap (README.md:45 is accurate but easy to misread), so a
  slow-drip server can stretch the handshake. CPU time (F6) is not covered by any deadline.
- `tls_hq` uses the realtime clock, so the host clock decides certificate validity.
- Wildcards are not checked against a public-suffix list (`*.co.uk`); this relies on CA policy.
- Caller-contract traps, not attacker-reachable:
  - `dns_query` with a hostname longer than the query buffer (`tls_dial` does not validate it, unlike
    `tls_init`);
  - `tls_write` with more than 16 GB (bound 999999 × 16384);
  - `/etc/resolv.conf` with more than 4096 consecutive blanks (tls.om:1869);
  - a trust bundle larger than TLSTSZ fails closed.
- CertificateRequest extensions are not validated. This is harmless because an empty Certificate is always
  sent.
- `chain_pres`/`chain_path` (x509.om ≈1703-1745) commit to the first presented candidate whose signature
  verifies, then check `chain_ca`, with no backtracking. This fails closed and affects availability only.

## Checked and found sound

**AEAD**
- `aead_chacha_open` (chacha.om:423-445): the Poly1305 tag is computed over AAD and ciphertext first, then the
  16 bytes are OR-accumulated with no early exit, and the one-time key is wiped. On mismatch the ciphertext
  region is zeroed and 1 is returned. Decryption runs only after the tag matches.
- `gcm_open1` (aesgcm.om:615-630): the same pattern (GHASH, then OR-accumulated tag compare, then wipe or CTR).
  `gcm_clear` wipes round keys, H and state on every path. The bitsliced AES has no secret-indexed tables
  (per the module notes; not timed).

**X25519** (x25519.om:298-326)
- Inputs are length-checked, the scalar is clamped, the ladder has a fixed count of 255 steps, cswap is masked,
  and the all-zero result is detected without branching (`x25519_fenc`).
- The workspace and scalar copy are wiped.
- tls.om:1035 propagates the zero result: the fakesh `zero` row expects ZERO_SHARED_SECRET.

**ECDSA** (ec.om:495-646)
- Public key: exactly `04‖X‖Y`, X < p and Y < p, and an on-curve equation check.
- DER: strict, short-form, minimal INTEGERs, no trailing bytes. An oversized INTEGER is replaced by n, so it
  fails the range check.
- r and s are both checked to lie in [1, n−1].
- A result at infinity is rejected, and x(R) mod n is compared with r.
- Curve constants are recorded as Wolfram-checked in test/ec/wolfram.txt (the file was not re-checked here).

**RSA**
- PKCS#1 v1.5 builds the whole k-byte block and compares it byte for byte; the block is never parsed. The
  DigestInfo prefixes are verbatim from RFC 8017.
- The PSS path checks the exact salt length. The X.509 layer requires salt = hash length, the trailer, the
  emBits mask and the zero PS.
- s ≥ n and a signature length different from the modulus length are both rejected.
- Only the exponent policy is weak (F6).

**Handshake order and authentication**
- Order: ServerHello → EncryptedExtensions → optional CertificateRequest → Certificate (empty context) →
  CertificateVerify → Finished (32 bytes, `tls_cteq`).
- Transcript ranges and key-schedule labels follow RFC 8446 §7.1. The RFC 8448 KAT covers them, and 4 library
  mutants were read.
- `TLHF == 0` is enforced at each key change.
- Every failure goes through `tls_abort` (phase 3, keys wiped).
- A `TLECV`/`TLEFIN` result is mapped to alert 51 (tls.om:1494).

**ServerHello**
- Exact version, HRR detection, constant-time session-ID echo, offered suite only, compression 0.
- Extensions are exactly supported_versions (0x0304) and an x25519 key_share, each at most once, with exact
  framing.
- Only TLS 1.3 is offered, so there is no downgrade path.

**EncryptedExtensions**
- Only SNI (empty), supported_groups and ALPN are accepted, with no duplicates. ALPN must be exactly the offered
  `http/1.1`.

**Record layer**
- Lengths: at most 16640 on the wire, at most 16384 of plaintext, ciphertext of at least 17.
- After keys are installed, only type 23 is accepted (CCS excepted, F7). A plaintext alert or handshake record
  is refused.
- Nonce = IV XOR seq. Seq refuses 2⁶⁴−1 instead of wrapping and is reset on key install.
- An all-zero inner plaintext is refused, and plaintext is exposed only after a successful open.
- An alert must be 2 bytes long. EOF without close_notify gives `-TLS_ERR_EOF`.
- Buffer offsets end exactly at TLCSZ = 119296.
  Correction (2026-10-27): true for the reviewed revision. Since cb6e46510 TLCSZ is 121696, and
  119296 is TLKDK, the start of the 2400-byte ML-KEM-768 decapsulation key that now ends at TLCSZ.

**Post-handshake**
- `tls_post` (bound 4096 messages per record ≤ 16384/4 is unreachable).
- NewSessionTicket is dropped, and anything other than NewSessionTicket or KeyUpdate is refused.

**Bounded loops checked unreachable**
- The ServerHello/EncryptedExtensions extension loops (bound 64) end within 4 passes.
- The padding scan (bound 20000 > 16624).
- `chain_index` (bound 100000 > 65536/5).
- `der_hdr` (≤ 3 length octets, minimal, never reads past the end).
- `rsa_bits`.
- The MGF1 loop (bound 999 > 1024/32).

**X.509**
- Strict DER for time (UTC pivot, GeneralizedTime, Z only), AlgorithmIdentifier (NULL or absent, exact PSS
  parameters, ECDSA without parameters), tbs/outer algorithm equality, version, and serial of 1..21 bytes.
- basicConstraints, keyUsage, EKU and SAN framing. Known extensions at most once; an unknown critical extension
  gives X5ECRIT.
- Leaf:
  - SAN dNSName only, with no CN fallback.
  - LDH host names; IP literals refused.
  - Wildcard only as the whole leftmost label.
  - Not a CA; digitalSignature; serverAuth.
  - RSA ≥ 2048 bits.
- Issuers:
  - Byte-equal DER names and consistent algorithm/key pairs.
  - CA:TRUE, pathLen, keyCertSign, EKU, validity.
  - Each presented certificate used at most once, depth ≤ 8, at most 16 entries.
- `x509_cv_verify`: the curve must match the scheme, rsa_pkcs1 is refused, and RSA must be ≥ 2048 bits. The leaf
  in TLLF is the same bytes `chain_leaf` validated.
- PEM/base64 decoding of the local trust store is strict.

**Constant time for secrets**
- `tls_cteq` (tls.om:172) for Finished, the session ID and ALPN.
- The README side-channel table (README.md:85-103) matches the code read; the DIT and padding caveats are
  stated.

**README claims checked**
- The API, the documented limits and the "not implemented" list match the code.
- Two claims are contradicted:
  - "Untrusted input must never trap" (README.md:165, and the module notes such as x509 and tls): see F1-F4 and
    F8.
  - Mutant counts: run.sh has 1 expectation + 4 KAT + 4 interop mutants = 9, matching "9/9". x509 run.sh has
    3 library + 1 expectation = 4, matching "4/4". These counts are accurate, but none of the mutants targets
    CertificateVerify (F9).

## Not reviewed in depth
- sha2.om and hkdf.om internals. Their KATs and Wycheproof counts are cited in the README and were not re-run.
- client.om and parity.py.
- AES S-box gate correctness.
