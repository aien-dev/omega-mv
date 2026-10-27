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
suite = b"\x13\x02" if mode == "suite" else b"\x13\x03"
ext = lambda t, d: t.to_bytes(2, "big") + len(d).to_bytes(2, "big") + d
sv = ext(43, b"\x03\x04")
if mode == "group":
    ks = ext(51, b"\x00\x17" + (65).to_bytes(2, "big") + b"\x04" + os.urandom(64))
elif mode == "zero":
    ks = ext(51, b"\x00\x1d\x00\x20" + bytes(32))
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
