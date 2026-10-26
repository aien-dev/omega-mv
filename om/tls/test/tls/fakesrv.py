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
#   sink        (bigsend.om) reads an 8-byte length T and T-8 pattern bytes (i -> 7 i mod 251),
#               answers SINKOK when every byte matches, else SINKBAD             -> BIGSEND OK
# HelloRetryRequest (RFC 8446 4.1.4); the client sends no x25519 share in ClientHello1 (FLAGS=2)
# unless noted. The server checks ClientHello2 (same random and session id, exactly one share for
# the requested group, the cookie echoed byte for byte) and answers a bad one with alert 47.
#   hrrok       HRR(x25519, cookie), then a correct handshake                    -> OK
#               (client with an x25519 share already in ClientHello1)           -> KEY_SHARE
#   hrrnock     HRR(x25519) without a cookie, then a correct handshake          -> OK
#   hrrccs      HRR, change_cipher_spec right after it (D.4), handshake         -> OK
#   hrrbadgrp   HRR selects secp256r1 (never offered)                           -> KEY_SHARE
#   hrr2        a second HRR instead of the ServerHello                         -> UNEXPECTED_MESSAGE
#   hrrsuite    HRR with TLS_CHACHA20, ServerHello with TLS_AES_128_GCM         -> CIPHER_SUITE
#   hrrnochg    HRR with supported_versions only (would not change ClientHello) -> HELLO_RETRY
#   hrrunk      HRR carrying early_data (recognised, not allowed in an HRR)    -> ILLEGAL_PARAMETER
#   hrrunk2     HRR carrying unknown extension type 0x0a0a                      -> EXTENSION
#   hrrsv       HRR without supported_versions                                  -> PROTOCOL_VERSION
#   hrrbigck    HRR with a 12001-byte cookie (ClientHello2 would not fit)       -> MESSAGE_TOO_BIG
# NewSessionTicket (RFC 8446 4.6.1) after the handshake, then the reply:
#   nstok       well-formed ticket (lifetime 604800, nonce, early_data ext)     -> OK
#   nstlife     ticket_lifetime 604801                                          -> TICKET
#   nstnotk     empty ticket                                                    -> DECODE
#   nsttrail    one byte after the extensions                                   -> DECODE
#   nstext      an extension whose length runs past the block                   -> DECODE
#   entryext    leaf CertificateEntry with a status_request extension           -> EXTENSION detail 21
# X25519MLKEM768 (0x11EC; draft-ietf-tls-ecdhe-mlkem): ML-KEM-768 encapsulation by kyber-py (the
# oracle; run with a python that has it), share = ct || x25519 public, secret = ss || x25519 secret
#   hyb         hybrid handshake                                                 -> OK
#   hybx        the client shares both groups, the server picks x25519           -> OK
#   hybshort    hybrid share one byte short                                      -> KEY_SHARE
#   hybzero     hybrid share whose x25519 half is the all-zero point             -> ZERO_SHARED_SECRET
#   hybct       one ciphertext bit flipped: implicit rejection, other secret     -> BAD_RECORD_MAC
import hashlib, hmac, os, socket, sys, time
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

    def open(self, rec):
        nonce = bytes(a ^ b for a, b in zip(self.iv, u(self.seq, 12)))
        self.seq += 1
        inner = self.aead.decrypt(nonce, rec[5:], rec[:5]).rstrip(b"\x00")
        return inner[-1], inner[:-1]


def next_secret(secret):
    return expand_label(secret, b"traffic upd", b"", 32)


def recv_exact(c, n):
    # MSG_WAITALL may return short on a socket with a timeout; loop until n bytes or EOF
    b = b""
    while len(b) < n:
        x = c.recv(n - len(b))
        if not x:
            break
        b += x
    return b


def recv_record(c):
    h = recv_exact(c, 5)
    if len(h) < 5:
        return None
    return h + recv_exact(c, int.from_bytes(h[3:5], "big"))


def parse_ch(ch):
    # random, session id, {group: share}, cookie (None if absent)
    p = 4 + 2
    rnd = ch[p:p + 32]
    p += 32
    sid = ch[p + 1:p + 1 + ch[p]]
    p += 1 + ch[p]
    p += 2 + int.from_bytes(ch[p:p + 2], "big")
    p += 1 + ch[p]
    end = p + 2 + int.from_bytes(ch[p:p + 2], "big")
    p += 2
    shares, cookie = {}, None
    while p < end:
        t, n = int.from_bytes(ch[p:p + 2], "big"), int.from_bytes(ch[p + 2:p + 4], "big")
        if t == 51:
            q = p + 6
            while q < p + 4 + n:
                g, kn = int.from_bytes(ch[q:q + 2], "big"), int.from_bytes(ch[q + 2:q + 4], "big")
                shares[g] = ch[q + 4:q + 4 + kn]
                q += 4 + kn
        if t == 44:
            cookie = ch[p + 6:p + 4 + n]
        p += 4 + n
    return rnd, sid, shares, cookie


def recv_hs(c):
    # next plaintext handshake record, skipping change_cipher_spec; None on an alert or EOF
    while True:
        rec = recv_record(c)
        if rec is None or rec[0] == 21:
            return None
        if rec[0] == 22:
            return rec


ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(("127.0.0.1", port)); ls.listen(1)
c, _ = ls.accept()
rec = recv_record(c)
ch = rec[5:]
rnd, sid, shares, _ = parse_ch(ch)
cpub = shares.get(0x1d)
CCS = b"\x14\x03\x03\x00\x01\x01"
ext = lambda t, b: u(t, 2) + u(len(b), 2) + b
hmsg = lambda t, b: bytes([t]) + u(len(b), 3) + b
HRRRAND = H(b"HelloRetryRequest")
trpre = ch
suite = b"\x13\x03"
ccsdone = False
if mode.startswith("hrr"):
    grp = 23 if mode == "hrrbadgrp" else 0x1d
    cookie = os.urandom(12001) if mode == "hrrbigck" else b"omega-cookie-" + os.urandom(8)
    hx = b"" if mode == "hrrsv" else ext(43, b"\x03\x04")
    if mode != "hrrnochg":
        hx += ext(51, u(grp, 2))
    if mode not in ("hrrnock", "hrrnochg"):
        hx += ext(44, u(len(cookie), 2) + cookie)
    else:
        cookie = None
    if mode == "hrrunk":
        hx += ext(42, b"")
    if mode == "hrrunk2":
        hx += ext(2570, b"")
    hb = b"\x03\x03" + HRRRAND + bytes([len(sid)]) + sid + suite + b"\x00" + u(len(hx), 2) + hx
    hrr = hmsg(2, hb)
    c.sendall(b"\x16\x03\x03" + u(len(hrr), 2) + hrr)
    if mode == "hrrccs":
        c.sendall(CCS); ccsdone = True
    rec = recv_hs(c)
    if rec is None:
        print("hrr: no ClientHello2", flush=True); c.close(); sys.exit(0)
    ch2 = rec[5:]
    rnd2, sid2, shares2, cookie2 = parse_ch(ch2)
    bad = [nm for nm, ok in (("random", rnd2 == rnd), ("sid", sid2 == sid), ("shares", list(shares2) == [grp]),
                             ("cookie", cookie2 == cookie), ("type", ch2[0] == 1)) if not ok]
    print("hrr: ClientHello2 bad=%s" % bad, flush=True)
    if bad:
        c.sendall(b"\x15\x03\x03\x00\x02\x02\x2f"); c.close(); sys.exit(0)
    if mode == "hrr2":
        c.sendall(b"\x16\x03\x03" + u(len(hrr), 2) + hrr)
        mode = "closepre2"
    trpre = b"\xfe\x00\x00\x20" + H(ch) + hrr + ch2
    ch, cpub, shares = ch2, shares2.get(0x1d), shares2
    if mode == "hrrsuite":
        suite = b"\x13\x01"
    if mode in ("hrrok", "hrrnock", "hrrccs", "hrrsuite"):
        mode = "ok"
hyb = mode.startswith("hyb") and mode != "hybx"
if mode == "hybx":
    mode = "ok"
OKMODES = ("ok", "cvbad", "cvkey", "cvskip", "empty1500", "empty50", "tickets1500",
           "slowsh", "kuexact", "kuover", "kurot", "entryext", "nstok", "nstlife", "nstnotk",
           "nsttrail", "nstext", "sink")
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
if mode == "closepre2":
    pass
elif mode == "closepre":
    c.sendall(b"\x15\x03\x03\x00\x02\x01\x00")
else:
    if mode == "ccspre":
        c.sendall(CCS)
    sk = X25519PrivateKey.generate()
    spub = sk.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    if hyb:
        from kyber_py.ml_kem import ML_KEM_768
        hs4588 = shares[4588]
        ss, ct = ML_KEM_768.encaps(hs4588[:1184])
        if mode == "hybct":
            ct = ct[:5] + bytes([ct[5] ^ 1]) + ct[6:]
        xpub = bytes(32) if mode == "hybzero" else spub
        kshare = ct + xpub
        if mode == "hybshort":
            kshare = kshare[:-1]
        exts = ext(43, b"\x03\x04") + ext(51, u(4588, 2) + u(len(kshare), 2) + kshare)
        shared = ss + sk.exchange(X25519PublicKey.from_public_bytes(hs4588[1184:]))
        mode = "ok"
    else:
        exts = ext(43, b"\x03\x04") + ext(51, b"\x00\x1d\x00\x20" + spub)
        shared = None
    body = b"\x03\x03" + os.urandom(32) + bytes([len(sid)]) + sid + suite + b"\x00" + u(len(exts), 2) + exts
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
    if not ccsdone:
        c.sendall(CCS)
    if mode == "ccs2":
        c.sendall(CCS)
    if mode == "ccsflood":
        c.sendall(CCS * 5000)
    tr = trpre + sh
    if shared is None:
        shared = sk.exchange(X25519PublicKey.from_public_bytes(cpub))
    early = extract(bytes(32), bytes(32))
    hs = extract(expand_label(early, b"derived", H(b""), 32), shared)
    s_hs = expand_label(hs, b"s hs traffic", H(tr), 32)
    c_hs = expand_label(hs, b"c hs traffic", H(tr), 32)
    sw = Keys(s_hs)
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
        lext = ext(5, b"") if mode == "entryext" else b""
        cl = b"".join(u(len(x), 3) + x + u(len(e), 2) + e for x, e in ((der("good.pem"), lext), (der("int.pem"), b"")))
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
        if mode in ("ok", "empty1500", "empty50", "tickets1500", "slowsh", "kuexact", "kuover", "kurot", "kuw", "hold", "sink") or mode.startswith("nst"):
            ms = extract(expand_label(hs, b"derived", H(b""), 32), bytes(32))
            s_ap = expand_label(ms, b"s ap traffic", H(tr), 32)
            aw = Keys(s_ap)
            if mode == "hold":
                c_ap = expand_label(ms, b"c ap traffic", H(tr), 32)
                keys = {"shared": shared, "early": early, "hs": hs, "c_hs": c_hs, "s_hs": s_hs,
                        "ms": ms, "c_ap": c_ap, "s_ap": s_ap}
                for nm, sec in (("c_hs", c_hs), ("s_hs", s_hs), ("c_ap", c_ap), ("s_ap", s_ap)):
                    keys[nm + "_key"] = expand_label(sec, b"key", b"", 32)
                    keys[nm + "_iv"] = expand_label(sec, b"iv", b"", 12)
                keys["c_fin"] = expand_label(c_hs, b"finished", b"", 32)
                keys["s_fin"] = expand_label(s_hs, b"finished", b"", 32)
                with open(os.environ["FAKESRV_KEYS"], "w") as f:
                    for nm, val in keys.items():
                        f.write("%s %s\n" % (nm, val.hex()))
            c.settimeout(10)
            # client: change_cipher_spec, Finished, then the request
            for _ in range(2 if mode in ("kuw", "sink") else 3):
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
            elif mode == "sink":
                # bigsend.om: 8-byte big-endian total T, then byte i = (7 i) mod 251 up to T
                c_ap = expand_label(ms, b"c ap traffic", H(tr), 32)
                ar = Keys(c_ap)
                got, recs, total, bad = bytearray(), 0, None, None
                while total is None or len(got) < total:
                    rec = recv_record(c)
                    if rec is None:
                        bad = "eof"; break
                    t, data = ar.open(rec)
                    recs += 1
                    if t != 23:
                        bad = "type%d" % t; break
                    got += data
                    if total is None and len(got) >= 8:
                        total = int.from_bytes(got[:8], "big")
                if bad is None:
                    base = bytes((i * 7) % 251 for i in range(251))
                    want = (base * (total // 251 + 1))[8:total]
                    if len(got) != total or bytes(got[8:]) != want:
                        bad = "content"
                print("sink: bytes=%d records=%d bad=%s" % (len(got), recs, bad), flush=True)
                out = aw.seal(23, b"SINKOK" if bad is None else b"SINKBAD") + aw.seal(21, b"\x01\x00")
            elif mode == "empty1500":
                out = b"".join(aw.seal(23, b"") for _ in range(1500))
            elif mode == "tickets1500":
                nst = hmsg(4, u(60, 4) + u(1, 4) + b"\x01\x00" + u(2, 2) + b"tk" + b"\x00\x00")
                out = b"".join(aw.seal(22, nst) for _ in range(1500))
            elif mode == "empty50":
                out = b"".join(aw.seal(23, b"") for _ in range(50))
            elif mode.startswith("nst"):
                life = 604801 if mode == "nstlife" else 604800
                tk = b"" if mode == "nstnotk" else os.urandom(40)
                ex = ext(42, u(1024, 4))
                if mode == "nstext":
                    ex = ex[:3] + b"\x05" + ex[4:]
                nb = u(life, 4) + os.urandom(4) + b"\x08" + os.urandom(8) + u(len(tk), 2) + tk + u(len(ex), 2) + ex
                if mode == "nsttrail":
                    nb += b"\x00"
                out = aw.seal(22, hmsg(4, nb))
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
            if mode in ("ok", "empty50", "slowsh", "kuexact", "kuover", "kurot", "hold", "nstok") or (mode == "kuw" and not out):
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
