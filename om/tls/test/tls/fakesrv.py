#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only (security review of omega-mv #813): a one-connection TLS 1.3 server on
# 127.0.0.1:PORT, written with python3 `cryptography`, that completes a real handshake
# (x25519, TLS_CHACHA20_POLY1305_SHA256, ecdsa_secp256r1_sha256 CertificateVerify with the
# interop PKI in DIR: good.pem + int.pem, good.key) and then misbehaves according to MODE.
# usage: fakesrv.py PORT DIR MODE
#   ok          a correct handshake, "HTTP/1.0 200 ok" reply, close_notify      -> OK
#   ccs2        two plaintext change_cipher_spec records                       -> UNEXPECTED_MESSAGE
#   ccsflood    5000 plaintext change_cipher_spec records                      -> UNEXPECTED_MESSAGE
#   ccspre      change_cipher_spec before the ServerHello                      -> UNEXPECTED_MESSAGE
#   ccslate     change_cipher_spec after the first encrypted record            -> UNEXPECTED_MESSAGE
#   encccs      an encrypted record whose inner content type is 20             -> UNEXPECTED_MESSAGE
#   frag        a 6000-byte Certificate sent one byte per record               -> MESSAGE_TOO_BIG
#   cvbad       CertificateVerify signature with one byte flipped              -> CERTIFICATE_VERIFY
#   cvkey       CertificateVerify signed with another key (other.key)          -> CERTIFICATE_VERIFY
#   empty1500   1500 empty application_data records after the handshake        -> UNEXPECTED_MESSAGE
#   empty50     50 empty application_data records, then the reply              -> OK
#   tickets1500 1500 NewSessionTicket messages after the handshake             -> UNEXPECTED_MESSAGE
#   closehs     encrypted close_notify after EncryptedExtensions               -> ALERT_RECEIVED 0
#   closepre    plaintext close_notify instead of the ServerHello              -> ALERT_RECEIVED 0
import hashlib, hmac, os, socket, sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

port, d, mode = int(sys.argv[1]), sys.argv[2], sys.argv[3]
u = lambda n, k: n.to_bytes(k, "big")
H = lambda b: hashlib.sha256(b).digest()


def expand_label(secret, label, ctx, n):
    info = u(n, 2) + bytes([6 + len(label)]) + b"tls13 " + label + bytes([len(ctx)]) + ctx
    out, t, i = b"", b"", 1
    while len(out) < n:
        t = hmac.new(secret, t + info + bytes([i]), hashlib.sha256).digest()
        out += t; i += 1
    return out[:n]


extract = lambda salt, ikm: hmac.new(salt, ikm, hashlib.sha256).digest()


class Keys:
    def __init__(self, secret):
        self.aead = ChaCha20Poly1305(expand_label(secret, b"key", b"", 32))
        self.iv = expand_label(secret, b"iv", b"", 12)
        self.seq = 0

    def seal(self, ctype, data):
        inner = data + bytes([ctype])
        nonce = bytes(a ^ b for a, b in zip(self.iv, u(self.seq, 12)))
        self.seq += 1
        hdr = b"\x17\x03\x03" + u(len(inner) + 16, 2)
        return hdr + self.aead.encrypt(nonce, inner, hdr)


def recv_record(c):
    h = c.recv(5, socket.MSG_WAITALL)
    if len(h) < 5:
        return None
    return h + c.recv(int.from_bytes(h[3:5], "big"), socket.MSG_WAITALL)


ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(("127.0.0.1", port)); ls.listen(1)
c, _ = ls.accept()
rec = recv_record(c)
ch = rec[5:]
p = 4 + 2 + 32
sid = ch[p + 1:p + 1 + ch[p]]
p += 1 + ch[p]
p += 2 + int.from_bytes(ch[p:p + 2], "big")
p += 1 + ch[p]
end = p + 2 + int.from_bytes(ch[p:p + 2], "big")
p += 2
cpub = None
while p < end:
    t, n = int.from_bytes(ch[p:p + 2], "big"), int.from_bytes(ch[p + 2:p + 4], "big")
    if t == 51:
        q = p + 6
        while q < p + 4 + n:
            g, kn = int.from_bytes(ch[q:q + 2], "big"), int.from_bytes(ch[q + 2:q + 4], "big")
            if g == 0x1d:
                cpub = ch[q + 4:q + 4 + kn]
            q += 4 + kn
    p += 4 + n
CCS = b"\x14\x03\x03\x00\x01\x01"
if mode == "closepre":
    c.sendall(b"\x15\x03\x03\x00\x02\x01\x00")
else:
    if mode == "ccspre":
        c.sendall(CCS)
    sk = X25519PrivateKey.generate()
    spub = sk.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    ext = lambda t, b: u(t, 2) + u(len(b), 2) + b
    exts = ext(43, b"\x03\x04") + ext(51, b"\x00\x1d\x00\x20" + spub)
    body = b"\x03\x03" + os.urandom(32) + bytes([len(sid)]) + sid + b"\x13\x03\x00" + u(len(exts), 2) + exts
    sh = b"\x02" + u(len(body), 3) + body
    c.sendall(b"\x16\x03\x03" + u(len(sh), 2) + sh)
    c.sendall(CCS)
    if mode == "ccs2":
        c.sendall(CCS)
    if mode == "ccsflood":
        c.sendall(CCS * 5000)
    tr = ch + sh
    shared = sk.exchange(X25519PublicKey.from_public_bytes(cpub))
    early = extract(bytes(32), bytes(32))
    hs = extract(expand_label(early, b"derived", H(b""), 32), shared)
    s_hs = expand_label(hs, b"s hs traffic", H(tr), 32)
    sw = Keys(s_hs)
    hmsg = lambda t, b: bytes([t]) + u(len(b), 3) + b
    ee = hmsg(8, b"\x00\x00")
    tr += ee
    c.sendall(sw.seal(22, ee))
    if mode == "ccslate":
        c.sendall(CCS)
    if mode == "encccs":
        c.sendall(sw.seal(20, b"\x01"))
    if mode == "closehs":
        c.sendall(sw.seal(21, b"\x01\x00"))
    if mode == "frag":
        big = hmsg(11, bytes(6000 - 4))
        for i in range(len(big)):
            try:
                c.sendall(sw.seal(22, big[i:i + 1]))
            except OSError:
                break
    if mode in ("ok", "cvbad", "cvkey", "empty1500", "empty50", "tickets1500"):
        der = lambda f: x509.load_pem_x509_certificate(open(f"{d}/{f}", "rb").read()).public_bytes(serialization.Encoding.DER)
        cl = b"".join(u(len(x), 3) + x + b"\x00\x00" for x in (der("good.pem"), der("int.pem")))
        cert = hmsg(11, b"\x00" + u(len(cl), 3) + cl)
        tr += cert
        kf = "other.key" if mode == "cvkey" else "good.key"
        key = serialization.load_pem_private_key(open(f"{d}/{kf}", "rb").read(), None)
        sig = bytearray(key.sign(b" " * 64 + b"TLS 1.3, server CertificateVerify\x00" + H(tr), ec.ECDSA(hashes.SHA256())))
        if mode == "cvbad":
            sig[-1] ^= 1
        cv = hmsg(15, b"\x04\x03" + u(len(sig), 2) + bytes(sig))
        tr += cv
        fin = hmsg(20, hmac.new(expand_label(s_hs, b"finished", b"", 32), H(tr), hashlib.sha256).digest())
        tr += fin
        c.sendall(sw.seal(22, cert) + sw.seal(22, cv) + sw.seal(22, fin))
        if mode in ("ok", "empty1500", "empty50", "tickets1500"):
            ms = extract(expand_label(hs, b"derived", H(b""), 32), bytes(32))
            aw = Keys(expand_label(ms, b"s ap traffic", H(tr), 32))
            c.settimeout(10)
            # client: change_cipher_spec, Finished, then the request
            for _ in range(3):
                if recv_record(c) is None:
                    break
            out = b""
            if mode == "empty1500":
                out = b"".join(aw.seal(23, b"") for _ in range(1500))
            elif mode == "tickets1500":
                nst = hmsg(4, u(60, 4) + u(1, 4) + b"\x01\x00" + u(2, 2) + b"tk" + b"\x00\x00")
                out = b"".join(aw.seal(22, nst) for _ in range(1500))
            elif mode == "empty50":
                out = b"".join(aw.seal(23, b"") for _ in range(50))
            if mode in ("ok", "empty50"):
                out += aw.seal(23, b"HTTP/1.0 200 ok\r\ncontent-length: 3\r\n\r\nok\n") + aw.seal(21, b"\x01\x00")
            try:
                c.sendall(out)
            except OSError:
                pass
c.settimeout(5)
try:
    while c.recv(4096):
        pass
except OSError:
    pass
c.close()
