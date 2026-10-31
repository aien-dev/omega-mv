# Security re-review: omega-mv #813 `feat/om-tls`, fix 38c80a85 over de2d884b

VERDICT: PASS

Scope: I read the current `/tmp/omtls/om/tls` tree. That tree is the working copy for 38c80a85 plus cdcf4229, a merge that touches only `.crumb`. I ran nothing: no git, no build, no suite. Every runtime statement below is reasoned from source. Test outcomes are NOT_RUN by me; the suite run belongs to TlsReviewFix.

## Per-finding status

| # | Status | Evidence (current line numbers) |
|---|---|---|
| F1 uniqueID trap | FIXED | `x509.om:1071-1083`: `k` starts at 0x80. Each 0x81 or 0x82 must be greater than the previous tag, and none is allowed when v1. The loop runs at most 3 times (0x81, 0x82, then 0xa3 returns), so `bound 4` cannot be reached. Tests: `cases.txt` `uid_*` (five, twice, reversed, 112), mutant `unique-id-order`. |
| F2 element bounds | FIXED | The `n > X5NMAX` (9999) guard is checked at the head of each pass, so pass 10000 returns before `bound 10000` can trap. It is present at `x509.om:858-862` (EKU), `899-903` (SAN), `992-996` (exts) and `1604-1608` (chain_host). The leaf `> X5LMAX` check at `1974` runs before `chain_leaf` and before any signature work. Tests: `eku/san_9999` OK and `eku/san/ext_10001` MALFORMED, `leaf_too_big`, mutants `elemcap` (the uncapped list traps, which counts as detected) and `leaf-size`. |
| F3 tls_hsmsg trap | FIXED | `tls.om:599-604`: at `k > 4000` it returns TLEBIG, below `bound 4096`. A CCS flood now ends at the second CCS because of the TLCCS check. Tests: interop `frag` and `ccsflood`, mutant handshake record cap (it reaches the bound, traps, and the row fails). |
| F4 tls_read trap | FIXED | `tls.om:1737-1742`: at `k > 1001` it calls `tls_abort` with TLEUNEX. `k` is counted per call and the loop returns as soon as data is pending. Tests: `empty1500`, `tickets1500`, `empty50` (OK), mutant read record cap. |
| F5 nameConstraints | FIXED | `x509.om:951-953`: OID 20 returns X5ENC whatever its criticality, before the critical/unknown branch. The check is in `x509_ext`, so it covers anchors, intermediates and the leaf. A store root carrying NC becomes unusable, which fails closed. Tests: `name_constraints_noncritical(_violated)`, mutant `name-constraints-noncritical`. |
| F6 RSA cost | FIXED | `x509_rsaexp` (611-631) accepts at most 4 content bytes, odd, ≥3. A bad exponent adds X5RBADE (2^32) to X5RBITS, and `x509_rsaok` (635) requires 2048..4096, so it is refused. It is enforced on presented intermediates in `chain_try` (1812, before `chain_link`/signature), on the leaf in `chain_leaf` (1896) and in `x509_cv_verify` (2035). Anchors only need ≥2048 at `chain_sig` (1653). The store is trusted, so a bad-e anchor is a local-config matter, not an attacker path. Tests: the `rsa_*` leaf and intermediate cases, `cv_rsa_6144`, `cv_rsa_e_2_32_plus_1`, and the 2 mutants. |
| F7 CCS policy | FIXED | `tls.om:532-539`: a plaintext CCS must be exactly `01`, length 1, with TLCCS==1, then TLCCS=2. TLCCS is 0 before ServerHello, set to 1 at 1088 together with TLRK=1, and set to 2 by the first protected record (552). An encrypted inner type 20 returns TLEUNEX (579-581). `tls_init` zeroes c[0..TLRB) including TLCCS at 496, and offset 496 does not overlap TLDET (492, 4 bytes) or TLEPK (512). Tests: `ccs2`, `ccsflood`, `ccspre`, `ccslate`, `encccs`, mutant CCS count. |
| F8 DNS | FIXED | Mask 64015 = 0xFA0F, expected 0x8000 (2000). The question is compared case-insensitively (`dns_qeq`). `dns_ans` follows the owner/CNAME chain through `dns_neq`, which is capped at 400 steps and uses `tls_at` (256 past the end), so it never traps. `dns_skip` returns 0 at 128 labels or more, before `bound 128`. `dns_rr` checks `p+10` and `rdlen` against `len r` before `dns_rrty`/`dns_rrip` read. The `dns_ans` `for an` loop is at most 65535. Tests: `dnsunit.om` (21 cases) and 3 DNS mutants (trap counts as detected). |
| F9 CV negative | FIXED | Interop `cvbad` and `cvkey`; imut `ret TLECV→ret 0`. A server that skips CV is not a row; see N2. |
| F10 test gaps | FIXED | The crafted x509 fixtures, fakesrv rows and DNS unit tests above. Interop rows compare an exact `TLSERR … (exit N)` string, so a trap (exit ≥100) fails the row. |
| F11 info | FIXED/DOC | `tls_close` (1803-1817) keeps TLPH=3. A close_notify during the handshake now returns TLEALRT detail 0 (`tls_hsmsg`), and it is tested by `closehs`, `closepre` and 2 mutants. The remaining items are documented, not changed: deadline, clock, PSL, CertificateRequest, and no path backtracking. |

## New holes checked (none found)
- TLCCS cannot be re-armed after it reaches 2: it is written only at 1088 (SH), 537 and 552, and SH is processed once. HRR is refused, so there is no second SH.
- The 4000-record handshake cap and the 1000-record read cap return errors rather than traps. They cannot fire on conforming GitHub or Gandi peers [INFERENCE: real servers do not send 1000 empty records or split a message over 4000 records].
- No consumer is confused by the X5RBADE offset in X5RBITS. All 4 reads (1653, 1851, 1896, 2035) treat it as "fails policy"; anchors pass ≥2048 by design.
- `dns_ptr` and `dns_neq` were checked for pointer loops, truncated pointers, out-of-range label bytes and label length >63. The 400-step cap ends in 1, and there is no indexing outside `tls_at`.

## Residual (non-blocking)
- **N1 INFO**, `tls.om:2168-2176` with `dns_query` at 1925. `dns_a4` builds the query in `new uchar 300`. `tls_cpy q 13 hn` runs before `dns_labels`, so a caller-supplied `hn` of 288 bytes or more given to `tls_dial` traps. This is caller-controlled, not network-controlled, and is unchanged from the first review. Fix: `if gt len hn 253 ret TLEDNS` at the top of `dns_a4`. (Closed in 2238f043, see below.)
- **N2 LOW test gap.** There is no fakesrv row where the server omits CertificateVerify and sends Finished. It would be refused by the message-order check, which existing rows do not isolate. Optional fix: a `cvskip` row. (Closed in 2238f043, see below.)
- **N3 note.** The README claim "each has a test that fails on the code before the fix" was not run by me (NOT_RUN). The mutant list and fixture names match the code.

## Delta 2238f043 (over cdcf4229, `om/tls` only)

VERDICT: PASS

Scope: I read the current tree at 2238f043. Nothing was run; test outcomes are NOT_RUN by me.

| Change | Assessment | Evidence |
|---|---|---|
| Trust anchors must be self-issued | Correct, fails closed | `chain_self` (`x509.om:1747-1760`) returns 1 only when the issuer DER and subject DER are byte-equal, using the record that `chain_anchor` (1777) just filled from the same `part st p l` view, so the offsets are consistent. `chain_alink` (1764-1769) returns X5EUNK for a non-self-issued entry, so `chain_anchor` skips it and keeps `res = X5EUNK`. An intermediate placed in the store can no longer short-cut the path; this matches OpenSSL without `-partial_chain`. Real roots in ca-certificates are self-issued [INFERENCE; the `real_*` X509_OK rows exercise this when run]. Tests: `cases.txt:26` `store_intermediate_anchor` → UNKNOWN_ISSUER, mutant `anchor-self-issued` (`run.sh:133`, its sed target matches `x509.om:1765` exactly). |
| `now >= notAfter` is expired | Correct, stricter than RFC 5280 and the same as OpenSSL | `chain_time` `x509.om:1631` `if le get r X5RNA nw` → X5EEXP. Valid iff notBefore ≤ now < notAfter. Header note at `x509.om:8-9` updated. Tests: `cases.txt:34-36` (notAfter−1 OK, ==notAfter EXPIRED, +1 EXPIRED), mutants `date-not-after` and `date-at-not-after` (`run.sh:129,131`; both sed patterns match the new line 1631, so neither is a no-op). |
| `dns_a4` hostname cap | Closes N1 | `tls.om:2164-2167`: `if gt len hn 253` returns TLEDNS before `new uchar 300`. The largest query is then 12 + 1 + 253 + 1 + 4 = 271 bytes, under 300. Mutant `run.sh:86` `dns_a4 name length unchecked`. |
| `cvskip` interop row | Closes N2 | `fakesrv.py:17,132,143-144`: Certificate then Finished, with CertificateVerify omitted from both the wire and the transcript. `interop.sh:163` expects exactly `TLSERR TLS_ERR_UNEXPECTED_MESSAGE`. |

New issues: none blocking.
- **D1 NIT (doc)** `test/x509/vectors/cases.txt:5` still says leaves are valid "... 2027-01-01 (1798761600) inclusive". After this change the bound is exclusive (`at_not_after` on line 35 expects EXPIRED). The comment should say "exclusive" or "until".
