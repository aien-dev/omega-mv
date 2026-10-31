# Porting note: catalog.py, omega_projects_pusher.py and allen_relay_mailer.py onto om/tls

Source: aien-dev/omega-mv PR #813, MERGED as 705c0f010e782408c270f66d67462bb46170e5b5 (2026-10-26).
Pin that merge commit. omc on main is 189ada82…3b54 (suites re-run on it).

## Build

- Run `sh om/tls/build.sh OMC your.om OUT`. It composes `om2`, then the nine TLS units, then your
  program.
- The units take 329 of omc's 512 functions per unit, which leaves about 180 for your code and
  OmegaNet's http/json.
- Local arrays: `c` needs `new uchar 119296` (TLCSZ) and the trust store `new uchar 262144`
  (TLSTSZ), together about 372 KiB of the 4 MiB local-array limit. Allocate both in `main`.
  Globals cannot be passed (error 19).
- Correction (2026-10-27): 119296 was TLCSZ when this was written. Since cb6e46510 (HelloRetryRequest
  and the hybrid key share) TLCSZ is 121696: TLKDK = 119296 plus the 2400-byte ML-KEM-768 decapsulation
  key. Allocate `new uchar TLCSZ` by name.

## Calls

```
e = tls_store st
e = tls_init c host 1                  (1 offers ALPN http/1.1; use 0 for SMTP)
fd = tls_dial host 443                 (465 for mail.gandi.net)
tls_deadline c add monons 30000000000
e = tls_connect c st int fd            (0, or a TLS_ERR_* code; tls_errname and tls_detail say why)
n = tls_write c req
n = tls_read c buf                     (repeat: >0 bytes, 0 = close_notify, <0 = -code)
tls_close c
x = fclose int fd
```

## Behaviour to rely on

- Every failure fails closed. There is no insecure option to look for.
- `TLS_ERR_EOF` (-4 from `tls_read`) means the TCP connection ended without close_notify. Treat it
  as truncation, not success.

## SMTP

- Port 465 (implicit TLS): the server's banner comes first, then EHLO, AUTH and the rest.
- Port 587 (STARTTLS): do the plaintext exchange on the fd first (banner, EHLO, STARTTLS, 220),
  then `tls_connect`. After the handshake the client speaks first.
- `om/tls/test/tls/client.om` modes `smtp` and `tsmtp` show both. They match Python ssl
  byte for byte against mail.gandi.net.

## HTTP

- The HTTP framing comes from OmegaNet's http.om; it calls `tls_read` and `tls_write` with
  `c = part io HTB HTL`.
- `client.om` mode `get` is a working minimal GET that uses `Connection: close`. It matched Python
  ssl on bodies of up to 680548 bytes.

## Limits

- IPv4 only, with DNS over UDP from `/etc/resolv.conf`.
- No revocation checking.
- No HelloRetryRequest, so servers that refuse x25519 are refused. All three target hosts accept
  x25519.
- See `om/tls/README.md`.
