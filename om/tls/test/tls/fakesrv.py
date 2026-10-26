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
#   cvskip      Certificate then Finished, no CertificateVerify                -> UNEXPECTED_MESSAGE
#   empty1500   1500 empty application_data records after the handshake        -> UNEXPECTED_MESSAGE
#   empty50     50 empty application_data records, then the reply              -> OK
#   tickets1500 1500 NewSessionTicket messages after the handshake             -> UNEXPECTED_MESSAGE
#   closehs     encrypted close_notify after EncryptedExtensions               -> ALERT_RECEIVED 0
#   closepre    plaintext close_notify instead of the ServerHello              -> ALERT_RECEIVED 0
# Outside-review fixes (key lifetime and handshake deadline); the client runs with OPTS:
#   loris       ServerHello trickled one byte per 250 ms for 8 s, then close   -> TIMEOUT (HSMS=2000)
#   slowsh      ServerHello one byte per 20 ms, then a correct handshake        -> OK (HSMS=10000)
#   kuexact     3 empty records, reply, close_notify = 5 under one key          -> OK (REC=5)
#   kuover      4 empty records, reply, close_notify = 6 under one key          -> SEQUENCE_LIMIT (REC=5)
#   kurot       4 empty, KeyUpdate at sequence 4, reply under the next key      -> OK (REC=5)
#   kuw:N       reads the request under the client's key following its KeyUpdates; more than N
#               records under one key, or no KeyUpdate at all, is answered with a fatal alert
#                                                                               -> OK (REC=N, SPLIT=1)
import hashlib, hmac, os, socket, sys, time
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305

port, d, mode = int(sys.argv[1]), sys.argv[2], sys.argv[3]
# MODE prefix "ap-" (the client in approved-only mode): secp256r1 key share and
# TLS_AES_128_GCM_SHA256 instead of x25519 and ChaCha20-Poly1305, e.g. ap-ok, ap-hold:4
ap = mode.startswith("ap-")
if ap:
    mode = mode[3:]
KL = 16 if ap else 32
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
        k = expand_label(secret, b"key", b"", KL)
        self.aead = AESGCM(k) if ap else ChaCha20Poly1305(k)
        self.iv = expand_label(secret, b"iv", b"", 12)
        self.seq = 0

    def seal(self, ctype, data):
        inner = data + bytes([ctype])
        nonce = bytes(a ^ b for a, b in zip(self.iv, u(self.seq, 12)))
        self.seq += 1
        hdr = b"\x17\x03\x03" + u(len(inner) + 16, 2)
        return hdr + self.aead.encrypt(nonce, inner, hdr)

    def open(self, rec):
        nonce = bytes(a ^ b for a, b in zip(self.iv, u(self.seq, 12)))
        self.seq += 1
        inner = self.aead.decrypt(nonce, rec[5:], rec[:5]).rstrip(b"\x00")
        return inner[-1], inner[:-1]


def next_secret(secret):
    return expand_label(secret, b"traffic upd", b"", 32)


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
            if g == (0x17 if ap else 0x1d):
                cpub = ch[q + 4:q + 4 + kn]
            q += 4 + kn
    p += 4 + n
CCS = b"\x14\x03\x03\x00\x01\x01"
OKMODES = ("ok", "cvbad", "cvkey", "cvskip", "empty1500", "empty50", "tickets1500",
           "slowsh", "kuexact", "kuover", "kurot")
kuw = mode.startswith("kuw:")  # kuw:N: the client must rekey before N records under one key
hold = 0.0
if mode.startswith("hold:"):   # hold:S: write every secret to $FAKESRV_KEYS, wait S s, then reply
    hold = float(mode[5:])
    mode = "hold"
    OKMODES += ("hold",)
if kuw:
    kulimit = int(mode[4:])
    mode = "kuw"
    OKMODES += ("kuw",)
if mode == "closepre":
    c.sendall(b"\x15\x03\x03\x00\x02\x01\x00")
else:
    if mode == "ccspre":
        c.sendall(CCS)
    if ap:
        sk = ec.generate_private_key(ec.SECP256R1())
        spub = sk.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    else:
        sk = X25519PrivateKey.generate()
        spub = sk.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    ext = lambda t, b: u(t, 2) + u(len(b), 2) + b
    exts = ext(43, b"\x03\x04") + ext(51, (b"\x00\x17" if ap else b"\x00\x1d") + u(len(spub), 2) + spub)
    body = b"\x03\x03" + os.urandom(32) + bytes([len(sid)]) + sid + (b"\x13\x01\x00" if ap else b"\x13\x03\x00") + u(len(exts), 2) + exts
    sh = b"\x02" + u(len(body), 3) + body
    shrec = b"\x16\x03\x03" + u(len(sh), 2) + sh
    if mode == "loris":
        # slow loris: one ServerHello byte every 250 ms, never finishing; the client must give up
        # on its handshake budget (TLS_ERR_TIMEOUT), not wait for this close (TLS_ERR_EOF)
        for i in range(32):
            try:
                c.sendall(shrec[i:i + 1]); time.sleep(0.25)
            except OSError:
                break
        c.close(); sys.exit(0)
    if mode == "slowsh":
        # slow but finishing: the whole ServerHello one byte every 20 ms (about 2 s)
        for i in range(len(shrec)):
            c.sendall(shrec[i:i + 1]); time.sleep(0.02)
    else:
        c.sendall(shrec)
    c.sendall(CCS)
    if mode == "ccs2":
        c.sendall(CCS)
    if mode == "ccsflood":
        c.sendall(CCS * 5000)
    tr = ch + sh
    if ap:
        shared = sk.exchange(ec.ECDH(), ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), cpub))
    else:
        shared = sk.exchange(X25519PublicKey.from_public_bytes(cpub))
    early = extract(bytes(32), bytes(32))
    hs = extract(expand_label(early, b"derived", H(b""), 32), shared)
    s_hs = expand_label(hs, b"s hs traffic", H(tr), 32)
    c_hs = expand_label(hs, b"c hs traffic", H(tr), 32)
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
    if mode in OKMODES:
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
        if mode == "cvskip":
            cv = b""
        tr += cv
        fin = hmsg(20, hmac.new(expand_label(s_hs, b"finished", b"", 32), H(tr), hashlib.sha256).digest())
        tr += fin
        c.sendall(sw.seal(22, cert) + (sw.seal(22, cv) if cv else b"") + sw.seal(22, fin))
        if mode in ("ok", "empty1500", "empty50", "tickets1500", "slowsh", "kuexact", "kuover", "kurot", "kuw", "hold"):
            ms = extract(expand_label(hs, b"derived", H(b""), 32), bytes(32))
            s_ap = expand_label(ms, b"s ap traffic", H(tr), 32)
            aw = Keys(s_ap)
            if mode == "hold":
                c_ap = expand_label(ms, b"c ap traffic", H(tr), 32)
                keys = {"shared": shared, "early": early, "hs": hs, "c_hs": c_hs, "s_hs": s_hs,
                        "ms": ms, "c_ap": c_ap, "s_ap": s_ap}
                for nm, sec in (("c_hs", c_hs), ("s_hs", s_hs), ("c_ap", c_ap), ("s_ap", s_ap)):
                    keys[nm + "_key"] = expand_label(sec, b"key", b"", KL)
                    keys[nm + "_iv"] = expand_label(sec, b"iv", b"", 12)
                keys["c_fin"] = expand_label(c_hs, b"finished", b"", 32)
                keys["s_fin"] = expand_label(s_hs, b"finished", b"", 32)
                with open(os.environ["FAKESRV_KEYS"], "w") as f:
                    for nm, val in keys.items():
                        f.write("%s %s\n" % (nm, val.hex()))
            c.settimeout(10)
            # client: change_cipher_spec, Finished, then the request
            for _ in range(2 if mode == "kuw" else 3):
                if recv_record(c) is None:
                    break
            out = b""
            if mode == "kuw":
                # read the request one record at a time under the client's application key,
                # following its KeyUpdates; refuse more than kulimit records under one key
                c_ap = expand_label(ms, b"c ap traffic", H(tr), 32)
                ar = Keys(c_ap)
                req, used, updates, bad = b"", 0, 0, None
                while b"\r\n\r\n" not in req:
                    rec = recv_record(c)
                    if rec is None:
                        bad = "eof"; break
                    used += 1
                    if used > kulimit:
                        bad = "limit"; break
                    t, data = ar.open(rec)
                    if t == 22 and data[:1] == b"\x18":
                        c_ap = next_secret(c_ap); ar = Keys(c_ap); used = 0; updates += 1
                    elif t == 23:
                        req += data
                    else:
                        bad = "type%d" % t; break
                if bad is None and updates == 0:
                    bad = "noupdate"
                print("kuw: updates=%d bad=%s" % (updates, bad), flush=True)
                if bad is not None:
                    out = aw.seal(21, b"\x02\x0a")
            elif mode == "empty1500":
                out = b"".join(aw.seal(23, b"") for _ in range(1500))
            elif mode == "tickets1500":
                nst = hmsg(4, u(60, 4) + u(1, 4) + b"\x01\x00" + u(2, 2) + b"tk" + b"\x00\x00")
                out = b"".join(aw.seal(22, nst) for _ in range(1500))
            elif mode == "empty50":
                out = b"".join(aw.seal(23, b"") for _ in range(50))
            elif mode == "kuexact":
                # with the client's limit at 5: records 0..4 = 3 empty, the reply, close_notify
                out = b"".join(aw.seal(23, b"") for _ in range(3))
            elif mode == "kuover":
                # 4 empty + reply + close_notify = 6 records under one key: the 6th must be refused
                out = b"".join(aw.seal(23, b"") for _ in range(4))
            elif mode == "kurot":
                # 4 empty, then KeyUpdate at the last allowed sequence number, reply under the next key
                out = b"".join(aw.seal(23, b"") for _ in range(4))
                out += aw.seal(22, hmsg(24, b"\x00"))
                s_ap = next_secret(s_ap); aw = Keys(s_ap)
            if mode == "hold":
                time.sleep(hold)
            if mode in ("ok", "empty50", "slowsh", "kuexact", "kuover", "kurot", "hold") or (mode == "kuw" and not out):
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
