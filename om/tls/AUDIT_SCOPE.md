# Audit scope: om/tls and seed/net

For an outside, independent security audit of the Omega TLS 1.3 client and the HTTP/1.1 unit it
transports. Revision: branch `audit/tls-outside-review` on aien-dev/omega-mv (base main `705c0f01`,
compiler `seed/out/omc` sha256 `189ada82e00bb440cb41351bd252cc68c54a1b1050051cc42a1df700081c3b54`).

## In scope

| path | what | lines of .om |
|---|---|---|
| `om/tls/sha2.om`, `hkdf.om` | SHA-256/384, HMAC, HKDF | see `wc -l om/tls/*.om` |
| `om/tls/x25519.om` | X25519 (RFC 7748) | |
| `om/tls/chacha.om`, `aesgcm.om` | the two AEADs | |
| `om/tls/ec.om`, `rsa.om`, `x509.om` | P-256/P-384 ECDSA verify, RSA PKCS#1 v1.5 and PSS verify, X.509 chain | |
| `om/tls/tls.om` | handshake, records, KeyUpdate, deadlines, wiping, DNS A lookup, DIT probe | |
| `seed/net/http.om`, `https.om`, `notls.om` | HTTP/1.1 client and server, HTTPS transport, plain stubs | |
| `seed/json/*.om` only where `seed/net` composes it | | |
| `om/tls/test/**`, `seed/net/tests/**` | suites, fake server, residue scan, timing driver, mutants | |
| `.github/workflows/om-tls-arm64.yml` | hosted arm64 rerun | |

Every implementation byte is `.om` compiled by the pinned `omc`. Python (`fakesrv.py`, `check.py`,
`residue.py`, `gen_crafted.py`, `parity.py`) and `openssl` are referees and fixtures only.

## Out of scope

- The compiler (`seed/omc/omc.om`, `seed/out/omc`) and its code generation; register and spill
  hygiene is a stated limit (`THREAT_MODEL.md`).
- Server-side TLS (there is none), client certificates, session resumption, 0-RTT, early data,
  ALPN beyond `http/1.1`, HTTP/2.
- Revocation (OCSP, CRL): absent by policy.
- The Linux kernel, `getrandom`, the socket layer, the Spark hardware.
- The rest of omega-mv.

## Questions the auditor is asked to answer

1. Does any branch, loop bound or memory index in the in-scope units depend on secret data?
2. Can any input from the network make the client trap (exit >= 100), loop without bound, or
   exceed `TLCSZ`/`TLSTSZ`?
3. Is the key schedule, the nonce construction and the transcript hash exactly RFC 8446?
4. Is chain verification (`x509.om`) sound against the Wycheproof-free X.509 corners: name
   constraints, EKU, key usage, path length, algorithm confusion, RSA key policy?
5. Are the key-lifetime, deadline and wiping paths reachable on every error path?
6. Does `seed/net/https.om` ever hand plaintext to a TLS socket or TLS records to a plain socket?

## How to reproduce

```
git clone https://github.com/aien-dev/omega-mv.git && cd omega-mv && git checkout audit/tls-outside-review
sha256sum seed/out/omc                      # must print 189ada82...3b54
sh om/tls/test/tls/run.sh "$PWD/seed/out/omc"              # add TLS_NET=1 for the live rows
python3 seed/net/tests/check.py --keep /tmp/netcheck       # add TLS_NET=1 for the live HTTPS GETs
python3 om/tls/test/tls/residue.py "$PWD/build/tls/tls/client/bin" "$PWD/build/tls/interop"
```

Prior inside reviews and their raw logs: `evidence/om-tls-review-2026-10-26/` (SHA-256 sums inside).

## Reporting

Findings go to aien-dev/omega-mv issues with the label `security`; embargoed findings to
drake@aienos.com. Each finding should name the unit, the line, the input that triggers it and, where
possible, a `fakesrv.py` mode or a vector file so the fix can carry a failing-before test.
