#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only: a one-connection fake TLS server on 127.0.0.1:PORT that reads the
# ClientHello and answers with one crafted ServerHello (plaintext handshake record), to drive the
# client's ServerHello refusal paths that a conforming server never reaches.
# usage: fakesh.py PORT MODE
#   tls12    legacy TLS 1.2 ServerHello, no supported_versions        -> PROTOCOL_VERSION
#   hrr      HelloRetryRequest random                                 -> HELLO_RETRY_UNSUPPORTED
#   suite    TLS_AES_256_GCM_SHA384 (not offered)                     -> CIPHER_SUITE
#   sid      session id not echoed                                    -> SESSION_ID_ECHO
#   group    key_share for secp256r1 (not offered)                    -> KEY_SHARE
#   zero     x25519 share of the all-zero point (shared secret 0)     -> ZERO_SHARED_SECRET
#   dupext   supported_versions twice                                 -> EXTENSION
#   trail    one byte after the extensions                            -> DECODE
# approved-only client mode (client OPTS 0,0,0,0,1; it offers only TLS_AES_128_GCM_SHA256 and
# secp256r1/secp384r1 key shares):
#   apchacha ChaCha20-Poly1305 chosen with a valid secp256r1 share       -> CIPHER_SUITE
#   apx25519 TLS_AES_128_GCM_SHA256 with an x25519 share                 -> KEY_SHARE
#   apbad256 secp256r1 share G with y + 1 (not on the curve)             -> KEY_SHARE
#   apbad384 secp384r1 share G with y + 1 (not on the curve)             -> KEY_SHARE
import hashlib, os, socket, sys

port, mode = int(sys.argv[1]), sys.argv[2]
ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(("127.0.0.1", port)); ls.listen(1)
c, _ = ls.accept()
hdr = c.recv(5, socket.MSG_WAITALL)
ch = c.recv(int.from_bytes(hdr[3:5], "big"), socket.MSG_WAITALL)
sid = ch[4 + 2 + 32 + 1:4 + 2 + 32 + 1 + ch[4 + 2 + 32]]
rnd = hashlib.sha256(b"HelloRetryRequest").digest() if mode == "hrr" else os.urandom(32)
if mode == "sid":
    sid = bytes(32)
suite = b"\x13\x02" if mode == "suite" else b"\x13\x01" if mode in ("apx25519", "apbad256", "apbad384") else b"\x13\x03"
ext = lambda t, d: t.to_bytes(2, "big") + len(d).to_bytes(2, "big") + d
sv = ext(43, b"\x03\x04")
G256 = (0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
        0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5)
G384 = (0xAA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7,
        0x3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F)
pt = lambda g, n, dy: b"\x04" + g[0].to_bytes(n, "big") + (g[1] + dy).to_bytes(n, "big")
if mode == "group":
    ks = ext(51, b"\x00\x17" + (65).to_bytes(2, "big") + b"\x04" + os.urandom(64))
elif mode == "zero":
    ks = ext(51, b"\x00\x1d\x00\x20" + bytes(32))
elif mode == "apchacha":
    ks = ext(51, b"\x00\x17\x00\x41" + pt(G256, 32, 0))
elif mode == "apbad256":
    ks = ext(51, b"\x00\x17\x00\x41" + pt(G256, 32, 1))
elif mode == "apbad384":
    ks = ext(51, b"\x00\x18\x00\x61" + pt(G384, 48, 1))
else:
    ks = ext(51, b"\x00\x1d\x00\x20" + os.urandom(32))
exts = ks if mode == "tls12" else sv + ks + (sv if mode == "dupext" else b"")
body = b"\x03\x03" + rnd + bytes([len(sid)]) + sid + suite + b"\x00" + len(exts).to_bytes(2, "big") + exts
if mode == "trail":
    body += b"\x00"
msg = b"\x02" + len(body).to_bytes(3, "big") + body
c.sendall(b"\x16\x03\x03" + len(msg).to_bytes(2, "big") + msg)
c.settimeout(5)
try:
    while c.recv(4096):
        pass
except OSError:
    pass
c.close()
