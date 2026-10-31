#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only: a scripted hostile TLS 1.3 server for the om/tls client (Omega has no TLS
# server, so every misbehaviour is driven from this side). One connection on 127.0.0.1:PORT,
# x25519 + TLS_CHACHA20_POLY1305_SHA256, written on python3 `cryptography` with its own record
# layer so every byte can be bent. The PKI comes from DIR (hostile.sh writes it): root.pem,
# int.pem, good.pem/good.key (EC P-256 leaf for localhost), rsa.pem/rsa.key (RSA-2048 leaf),
# other.key (an unrelated EC key).
# usage: hostile.py PORT DIR CASE            (CASE: a row of hostile.tsv, or ch_policy)
#        hostile.py PORT DIR replay CHAINPEM  (badssl.sh: present CHAINPEM, then a CertificateVerify
#                                              made with good.key, which is never the leaf's key)
# Prints "READY" once listening, then "SRV key=value ..." facts about what the client sent:
#   alert=N (the client's alert description, decrypted when it was protected; none when it closed
#   without one), cfin=ok|bad|none (the client Finished checked against our transcript),
#   ccert=empty|none|other, req=1 (an HTTP request arrived), ku=N (client KeyUpdates).
import hashlib, hmac, os, socket, sys, threading, time
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature, encode_dss_signature
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

port, d, mode = int(sys.argv[1]), sys.argv[2], sys.argv[3]
chainpem = sys.argv[4] if mode == "replay" else None
u = lambda n, k: n.to_bytes(k, "big")
H = lambda b: hashlib.sha256(b).digest()
ext = lambda t, b: u(t, 2) + u(len(b), 2) + b
hmsg = lambda t, b: bytes([t]) + u(len(b), 3) + b
rec = lambda t, b, v=b"\x03\x03": bytes([t]) + v + u(len(b), 2) + b
CCS = rec(20, b"\x01")
facts = {}


def say(**kw):
    facts.update(kw)


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
        self.secret = secret
        self.aead = ChaCha20Poly1305(expand_label(secret, b"key", b"", 32))
        self.iv = expand_label(secret, b"iv", b"", 12)
        self.seq = 0

    def nonce(self):
        n = bytes(a ^ b for a, b in zip(self.iv, u(self.seq, 12)))
        self.seq += 1
        return n

    # seal: inner = data || type || pad zeros; outer type and a raw inner override for tests
    def seal(self, ctype, data, pad=0, outer=23, inner=None):
        if inner is None:
            inner = data + bytes([ctype]) + bytes(pad)
        hdr = bytes([outer]) + b"\x03\x03" + u(len(inner) + 16, 2)
        return hdr + self.aead.encrypt(self.nonce(), inner, hdr)

    def open(self, r):
        inner = self.aead.decrypt(self.nonce(), r[5:], r[:5]).rstrip(b"\x00")
        return inner[-1], inner[:-1]


# A reader thread drains the socket as bytes arrive: a client that aborts with our later records
# still unread closes with a TCP RST, and the kernel drops whatever the server had not yet read,
# which would lose the client's alert.
inbuf = {"b": b"", "eof": False}
cv_in = threading.Condition()


def reader(c):
    while True:
        try:
            b = c.recv(65536)
        except OSError:
            b = b""
        with cv_in:
            if not b:
                inbuf["eof"] = True
            inbuf["b"] += b
            cv_in.notify_all()
        if not b:
            return


def take(n):
    with cv_in:
        cv_in.wait_for(lambda: len(inbuf["b"]) >= n or inbuf["eof"], timeout=20)
        if len(inbuf["b"]) < n:
            return None
        out, inbuf["b"] = inbuf["b"][:n], inbuf["b"][n:]
        return out


def recv_record(c):
    h = take(5)
    if h is None:
        return None
    b = take(int.from_bytes(h[3:5], "big"))
    return None if b is None else h + b


def send(c, b):
    try:
        c.sendall(b)
        return True
    except OSError:
        return False


def paced(b, quiet=True):
    """send one record at a time and stop once the client has aborted, so it never closes with our
    records unread (that close is a TCP RST, which can drop its alert before we read it).
    quiet: the client writes nothing while we send (handshake flight), so any byte from it means it
    aborted; otherwise (after the handshake, where it may answer a KeyUpdate) only its close counts."""
    # the client's middlebox-compatibility ChangeCipherSpec (RFC 8446 D.4) is not an abort
    noccs = lambda: inbuf["b"].replace(b"\x14\x03\x03\x00\x01\x01", b"")
    done = (lambda: noccs() or inbuf["eof"]) if quiet else (lambda: inbuf["eof"])
    while b:
        n = 5 + int.from_bytes(b[3:5], "big")
        with cv_in:
            if done():
                return
        if not send(c, b[:n]):
            return
        b = b[n:]
        with cv_in:
            cv_in.wait_for(done, timeout=0.05)

ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(("127.0.0.1", port)); ls.listen(1)
print("READY", flush=True)
c, _ = ls.accept()
c.settimeout(20)
threading.Thread(target=reader, args=(c,), daemon=True).start()

# ---- ClientHello ----
r = recv_record(c)
ch = r[5:]
p = 4 + 2 + 32
sid = ch[p + 1:p + 1 + ch[p]]
p += 1 + ch[p]
suites = [int.from_bytes(ch[p + 2 + i:p + 4 + i], "big") for i in range(0, int.from_bytes(ch[p:p + 2], "big"), 2)]
p += 2 + int.from_bytes(ch[p:p + 2], "big")
comps = list(ch[p + 1:p + 1 + ch[p]])
p += 1 + ch[p]
end = p + 2 + int.from_bytes(ch[p:p + 2], "big")
p += 2
cpub, exts, extbody = None, [], {}
while p < end:
    t, n = int.from_bytes(ch[p:p + 2], "big"), int.from_bytes(ch[p + 2:p + 4], "big")
    exts.append(t); extbody[t] = ch[p + 4:p + 4 + n]
    if t == 51:
        q = p + 6
        while q < p + 4 + n:
            g, kn = int.from_bytes(ch[q:q + 2], "big"), int.from_bytes(ch[q + 2:q + 4], "big")
            if g == 0x1d:
                cpub = ch[q + 4:q + 4 + kn]
            q += 4 + kn
    p += 4 + n
lst = lambda b, w, o: [int.from_bytes(b[o + i:o + i + w], "big") for i in range(0, len(b) - o, w)]

if mode == "ch_policy":
    # ClientHello policy (no handshake): what can ever be negotiated with this client
    sv = lst(extbody.get(43, b"\x00"), 2, 1)
    groups = lst(extbody.get(10, b"\x00\x00"), 2, 2)
    sigs = lst(extbody.get(13, b"\x00\x00"), 2, 2)
    ks = []
    kb = extbody.get(51, b"\x00\x00")
    q = 2
    while q < len(kb):
        ks.append(int.from_bytes(kb[q:q + 2], "big")); q += 4 + int.from_bytes(kb[q + 2:q + 4], "big")
    bad = []
    if ch[4:6] != b"\x03\x03": bad.append("legacy_version")
    if sv != [0x0304]: bad.append("supported_versions")
    if not set(suites) <= {0x1301, 0x1302, 0x1303}: bad.append("suites")
    if comps != [0]: bad.append("compression")
    if not set(groups) <= {0x1d, 0x17, 0x18, 0x11ec, 0x6399}: bad.append("groups")
    if any(s in (0x0201, 0x0203, 0x0202) for s in sigs): bad.append("sha1_sigalgs")
    if 41 in exts or 42 in exts or 45 in exts: bad.append("psk_or_early_data")
    if len(sid) != 32: bad.append("session_id")
    print("SRV ch_policy versions=%s suites=%s comp=%s groups=%s key_shares=%s sigalgs=%s exts=%s bad=%s"
          % (",".join("%04x" % x for x in sv), ",".join("%04x" % x for x in suites), comps,
             ",".join("%04x" % x for x in groups), ",".join("%04x" % x for x in ks),
             ",".join("%04x" % x for x in sigs), ",".join(str(x) for x in exts), ",".join(bad) or "none"), flush=True)
    send(c, rec(21, b"\x02\x28"))
    c.close(); sys.exit(0)

# ---- ServerHello ----
sk = X25519PrivateKey.generate()
spub = sk.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
shared = sk.exchange(X25519PublicKey.from_public_bytes(cpub))
share = spub
if mode == "sh_ks_hibit":
    share = spub[:31] + bytes([spub[31] | 0x80])      # RFC 7748 section 5: the receiver masks bit 255
if mode == "sh_ks_lo1":
    share = b"\x01" + bytes(31)
if mode == "sh_ks_lo8":
    share = bytes.fromhex("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800")
ks = ext(51, b"\x00\x1d\x00\x20" + share)
if mode == "sh_kslen31":
    ks = ext(51, b"\x00\x1d\x00\x1f" + share[:31])
sv = ext(43, {"sh_sv0303": b"\x03\x03", "sh_sv7f": b"\x7f\x1c"}.get(mode, b"\x03\x04"))
if mode == "sh_svlen":
    sv = ext(43, b"\x03\x04\x00")
shx = {"sh_noks": sv, "sh_dupks": sv + ks + ks,
       "sh_ext_unsol": sv + ks + ext(0x0a0a, b""), "sh_ext_psk": sv + ks + ext(41, b"\x00\x00"),
       "sh_ext_sni": sv + ks + ext(0, b""), "sh_ext_cookie": sv + ks + ext(44, b"\x00\x02ab"),
       "sh_ext_early": sv + ks + ext(42, b""),
       "sh_ext_ksalias": sv + ext(0x0a0a, ks[4:]),
       "sh_dgrd12": ks, "sh_dgrd11": ks}.get(mode, sv + ks)
rnd = os.urandom(32)
if mode == "sh_dgrd12":
    rnd = rnd[:24] + b"DOWNGRD\x01"
if mode == "sh_dgrd11":
    rnd = rnd[:24] + b"DOWNGRD\x00"
legacy = {"sh_dgrd11": b"\x03\x02"}.get(mode, b"\x03\x03")
sidf = bytes([len(sid)]) + sid
if mode == "sh_sid33":
    sidf = b"\x21" + sid + b"\x00"
suite = {"sh_suite_tls12": b"\xc0\x2f"}.get(mode, b"\x13\x03")
comp = b"\x01" if mode == "sh_comp1" else b"\x00"
extlen = u(len(shx) + (5 if mode == "sh_extlen" else 0), 2)
body = legacy + rnd + sidf + suite + comp + extlen + shx
sh = hmsg(2, body)

pre = b""
if mode == "rec_unknown_type":
    pre = rec(0x63, b"\x00\x01")
if mode == "rec_appdata_plain":
    pre = rec(23, b"GET")
if mode == "rec_alert_frag":
    pre = rec(21, b"\x02")
if mode == "rec_alert_zero":
    pre = rec(21, b"")
if mode == "rec_alert_unknown_plain":
    pre = rec(21, b"\x02\xc8")
if mode == "sh_hs_zero":
    pre = rec(22, b"")
if mode == "rec_overflow_plain":
    shrec = rec(22, sh + bytes(16385 - len(sh)))     # 2^14 + 1 bytes of plaintext fragment
elif mode == "sh_twice":
    shrec = rec(22, sh + sh)
elif mode == "sh_split":
    shrec = rec(22, sh[:40]) + rec(22, sh[40:])
elif mode == "sh_rec0301":
    shrec = rec(22, sh, b"\x03\x01")
else:
    shrec = rec(22, sh)
tr = ch + sh
early = extract(bytes(32), bytes(32))
hs = extract(expand_label(early, b"derived", H(b""), 32), shared)
s_hs = expand_label(hs, b"s hs traffic", H(tr), 32)
c_hs = expand_label(hs, b"c hs traffic", H(tr), 32)
sw = Keys(s_hs)

# the client's records: plaintext alerts, then protected under c_hs, then c_ap (set later)
cr_state = {"keys": [Keys(c_hs)], "alert": None}


def client_records(until, limit=4000):
    """read the client's records; until(type, data) returns True to stop. EOF/timeout stops."""
    for _ in range(limit):
        try:
            r = recv_record(c)
        except OSError:
            return False
        if r is None:
            return False
        t = r[0]
        if t == 20:
            continue
        if t == 21:
            cr_state["alert"] = r[6] if len(r) > 6 else -1
            say(alert=cr_state["alert"])
            return False
        if t == 23:
            ok = False
            for k in list(cr_state["keys"]):
                seq = k.seq
                try:
                    it, data = k.open(r)
                    ok = True
                    break
                except Exception:
                    k.seq = seq
            if not ok:
                say(undecryptable=1)
                continue
            if it == 21:
                cr_state["alert"] = data[1] if len(data) > 1 else -1
                say(alert=cr_state["alert"])
                return False
            if until(it, data):
                return True
    return False


if mode == "sh_plus_ee":
    shrec = rec(22, sh + hmsg(8, b"\x00\x00"))
paced(pre + shrec)
ccs = {"ccs_val2": rec(20, b"\x02"), "ccs_len2": rec(20, b"\x01\x01"), "ccs_zero": rec(20, b"")}.get(mode, CCS)
paced(ccs)

# ---- encrypted flight ----
def cert_msg(ders, ctx=b"", entry_ext=b""):
    cl = b"".join(u(len(x), 3) + x + u(len(entry_ext), 2) + entry_ext for x in ders)
    return hmsg(11, bytes([len(ctx)]) + ctx + u(len(cl), 3) + cl)


def load_ders(path):
    data = open(path, "rb").read()
    out = []
    for blk in data.split(b"-----END CERTIFICATE-----")[:-1]:
        out.append(x509.load_pem_x509_certificate(blk + b"-----END CERTIFICATE-----\n").public_bytes(serialization.Encoding.DER))
    return out


ee_exts = {"ee_ks": ext(51, b"\x00\x1d\x00\x20" + spub), "ee_sv": ext(43, b"\x03\x04"),
           "ee_psk": ext(41, b"\x00\x00"), "ee_dup": ext(0, b"") + ext(0, b""),
           "ee_unsol": ext(0x0a0a, b""), "ee_mfl": ext(1, b"\x01"),
           "ee_alpn_other": ext(16, b"\x00\x03\x02h2"),
           "ee_alpn_two": ext(16, b"\x00\x0c\x08http/1.1\x02h2")}.get(mode, b"")
ee = hmsg(8, u(len(ee_exts), 2) + ee_exts + (b"\x00" if mode == "ee_trail" else b""))
if mode == "ee_bigdecl":
    ee = b"\x08\xff\xff\xff" + bytes(64)
rsaleaf = mode in ("cv_pkcs1",)
if mode == "replay":
    ders = load_ders(chainpem)
else:
    ders = load_ders(f"{d}/{'rsa' if rsaleaf else 'good'}.pem") + load_ders(f"{d}/int.pem")
cctx, eext = b"", b""
if mode == "cert_ctx":
    cctx = b"\x01\x02"
if mode == "cert_entry_ext":
    eext = ext(0x0a0a, b"")
cert = cert_msg(ders, cctx, eext)
if mode == "cert_empty":
    cert = hmsg(11, b"\x00\x00\x00\x00")
if mode == "cert_zero_entry":
    cert = hmsg(11, b"\x00" + u(5, 3) + b"\x00\x00\x00\x00\x00")
if mode == "cert_list_overrun":
    cert = cert[:5] + u(int.from_bytes(cert[5:8], "big") + 10, 3) + cert[8:]
cr = b""
if mode in ("cr_hs", "cr_ctx", "cr_nosigalgs"):
    crx = b"" if mode == "cr_nosigalgs" else ext(13, b"\x00\x04\x04\x03\x08\x04")
    cr = hmsg(13, (b"\x02ab" if mode == "cr_ctx" else b"\x00") + u(len(crx), 2) + crx)


def cv_msg(trans):
    content = b" " * 64 + b"TLS 1.3, " + (b"client" if mode == "cv_ctxclient" else b"server") + b" CertificateVerify\x00" + H(trans)
    if mode == "cv_pkcs1":
        key = serialization.load_pem_private_key(open(f"{d}/rsa.key", "rb").read(), None)
        return hmsg(15, b"\x04\x01" + u(256, 2) + key.sign(content, padding.PKCS1v15(), hashes.SHA256()))
    key = serialization.load_pem_private_key(open(f"{d}/good.key", "rb").read(), None)
    sig = key.sign(content, ec.ECDSA(hashes.SHA256()))
    if mode == "cv_highs":
        r_, s_ = decode_dss_signature(sig)
        n = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551
        sig = encode_dss_signature(r_, n - s_)
    scheme = {"cv_unoffered": b"\x06\x03", "cv_keymismatch": b"\x08\x04"}.get(mode, b"\x04\x03")
    if mode == "cv_empty":
        sig = b""
    return hmsg(15, scheme + u(len(sig), 2) + sig + (b"\x00" if mode == "cv_trail" else b""))


fl = []          # (kind, message) in order; kind "hs" is sealed as handshake
order = ["ee", "cr", "cert", "cv", "fin"]
if mode == "order_cert_first":
    order = ["cert", "ee", "cv", "fin"]
if mode == "order_fin_noauth":
    order = ["ee", "fin"]
if mode == "order_cv_first":
    order = ["ee", "cv", "cert", "fin"]
if mode == "ee_twice":
    order = ["ee", "ee", "cert", "cv", "fin"]
if mode == "cv_twice":
    order = ["ee", "cert", "cv", "cv", "fin"]
msgs = []
for k in order:
    if k == "ee":
        m = ee
    elif k == "cr":
        if not cr:
            continue
        m = cr
    elif k == "cert":
        m = cert
    elif k == "cv":
        m = cv_msg(tr)
    else:
        vd = hmac.new(expand_label(s_hs, b"finished", b"", 32), H(tr), hashlib.sha256).digest()
        if mode == "fin_bad":
            vd = vd[:5] + bytes([vd[5] ^ 1]) + vd[6:]
        if mode == "fin_short":
            vd = vd[:31]
        if mode == "fin_long":
            vd = vd + b"\x00"
        m = hmsg(20, vd)
    tr += m
    msgs.append(m)
fin = msgs[-1]

# application secrets (only meaningful when the transcript above is the real one)
ms = extract(expand_label(hs, b"derived", H(b""), 32), bytes(32))
s_ap = expand_label(ms, b"s ap traffic", H(tr), 32)
c_ap = expand_label(ms, b"c ap traffic", H(tr), 32)
aw = Keys(s_ap)
out = b""
if mode == "hs_coalesce":
    out += sw.seal(22, b"".join(msgs))
elif mode == "hs_frag_ee":
    out += sw.seal(22, msgs[0][:3]) + sw.seal(22, msgs[0][3:]) + b"".join(sw.seal(22, m) for m in msgs[1:])
elif mode == "fin_plus_nst":
    nst = hmsg(4, u(60, 4) + u(1, 4) + b"\x01\x00" + u(2, 2) + b"tk" + b"\x00\x00")
    out += b"".join(sw.seal(22, m) for m in msgs[:-1]) + sw.seal(22, fin + nst)
else:
    for i, m in enumerate(msgs):
        if mode == "hs_pad_big" and i == 0:
            out += sw.seal(22, m, pad=16000)
        elif mode == "rec_allzero" and i == 0:
            out += sw.seal(0, b"", inner=bytes(40))
        elif mode == "rec_innertype_unk" and i == 0:
            out += sw.seal(0x63, m)
        elif mode == "rec_overflow_cipher" and i == 0:
            out += sw.seal(22, m, pad=16384 + 256 + 1 - 16 - len(m) - 1)  # ciphertext 2^14+256+1
        elif mode == "rec_overflow_inner" and i == 0:
            out += sw.seal(22, m, pad=16386 - len(m) - 1)                       # inner 2^14+2
        elif mode == "rec_short" and i == 0:
            out += b"\x17\x03\x03\x00\x05" + os.urandom(5)
        elif mode == "rec_replay" and i == 0:
            rr = sw.seal(22, m)
            out += rr + rr
        elif mode == "rec_outer_type22" and i == 0:
            out += sw.seal(22, m, outer=22)
        elif mode == "hs_appdata_early" and i == 1:
            out += sw.seal(23, b"early") + sw.seal(22, m)
        elif mode == "hs_plain_alert" and i == 1:
            out += rec(21, b"\x02\x28") + sw.seal(22, m)
        elif mode == "ee_zero_frag" and i == 0:
            out += sw.seal(22, b"") + sw.seal(22, m)
        else:
            # a message over 2^14 (a replayed big chain) goes out in 2^14-byte records (5.1)
            out += b"".join(sw.seal(22, m[k:k + 16384]) for k in range(0, len(m), 16384))
paced(out)
if mode == "fin_twice":
    # after our Finished the client legitimately writes its own flight, which stops paced(); the
    # second Finished must still go out (it is the case) and its sequence number must be used
    send(c, aw.seal(22, hmsg(20, bytes(32))))

# ---- the client's flight: [Certificate], Finished, then the request under c_ap ----
ctr = tr
st = {"fin": False, "req": b"", "ku": 0}


def on_hs(it, data):
    global ctr
    if it != 22:
        say(cunexpected=it)
        return True
    q = 0
    while q + 4 <= len(data):
        ht, hl = data[q], int.from_bytes(data[q + 1:q + 4], "big")
        m = data[q:q + 4 + hl]
        if ht == 11:
            say(ccert="empty" if m[4 + 1 + m[4]:] == b"\x00\x00\x00" else "other")
            ctr += m
        elif ht == 20:
            want = hmac.new(expand_label(c_hs, b"finished", b"", 32), H(ctr), hashlib.sha256).digest()
            say(cfin="ok" if m[4:] == want else "bad")
            st["fin"] = True
            return True
        else:
            say(chs=ht)
        q += 4 + hl
    return False


ok = client_records(on_hs)
if not ok or not st["fin"]:
    facts.setdefault("cfin", "none")
    print("SRV " + " ".join("%s=%s" % kv for kv in facts.items()), flush=True)
    c.close(); sys.exit(0)
ar = Keys(c_ap)
cr_state["keys"] = [ar]
capk = [c_ap]


def on_app(it, data):
    if it == 22 and data[:1] == b"\x18":
        st["ku"] += 1
        capk[0] = expand_label(capk[0], b"traffic upd", b"", 32)
        cr_state["keys"][0] = Keys(capk[0])
        return False
    if it == 23:
        st["req"] += data
        return b"\r\n\r\n" in st["req"]
    say(capp=it)
    return True


client_records(on_app)
say(req=int(b"\r\n\r\n" in st["req"]))
REPLY = b"HTTP/1.0 200 ok\r\ncontent-length: 3\r\n\r\nok\n"


def reply(w):
    return w.seal(23, REPLY) + w.seal(21, b"\x01\x00")


nst = hmsg(4, u(60, 4) + u(1, 4) + b"\x01\x00" + u(2, 2) + b"tk" + b"\x00\x00")
o, tail = b"", b""
if mode == "post_cr":
    o = aw.seal(22, hmsg(13, b"\x02ab\x00\x08" + ext(13, b"\x00\x02\x04\x03"))) + reply(aw)
elif mode == "post_unknown_hs":
    o = aw.seal(22, hmsg(99, b"\x00\x00")) + reply(aw)
elif mode == "post_sh":
    o = aw.seal(22, sh) + reply(aw)
elif mode == "post_ku_req":
    o = aw.seal(22, hmsg(24, b"\x01"))
    s2 = expand_label(s_ap, b"traffic upd", b"", 32); aw = Keys(s2)
    o += reply(aw)
elif mode == "post_ku_bad":
    o = aw.seal(22, hmsg(24, b"\x02")) + reply(aw)
elif mode == "post_ku_len":
    o = aw.seal(22, hmsg(24, b"\x00\x00")) + reply(aw)
elif mode == "post_ku_flood":
    s2 = s_ap
    for _ in range(1001):          # tls_read gives up after 1001 records without data
        o += aw.seal(22, hmsg(24, b"\x01"))
        s2 = expand_label(s2, b"traffic upd", b"", 32); aw = Keys(s2)
    tail = reply(aw)
elif mode == "post_nst_split":
    o = aw.seal(22, nst[:7]) + aw.seal(22, nst[7:]) + reply(aw)
elif mode == "post_interleave":
    o = aw.seal(22, nst[:7]) + aw.seal(23, b"x") + aw.seal(22, nst[7:]) + reply(aw)
elif mode == "post_nst_trail":
    o = aw.seal(22, hmsg(4, nst[4:] + b"\x00")) + reply(aw)
elif mode == "post_plain_close":
    o = aw.seal(23, b"HTTP/1.0 200 ok\r\ncontent-length: 100\r\n\r\npart") + rec(21, b"\x01\x00")
elif mode == "post_trunc":
    o = aw.seal(23, b"HTTP/1.0 200 ok\r\ncontent-length: 100\r\n\r\npart")
elif mode == "post_trunc_full":
    o = aw.seal(23, REPLY)
elif mode == "post_alert_unknown":
    o = aw.seal(23, b"HTTP/1.0 200 ok\r\n") + aw.seal(21, b"\x02\xc8")
elif mode == "post_alert_warn":
    o = aw.seal(23, b"HTTP/1.0 200 ok\r\n") + aw.seal(21, b"\x01\x2a")
elif mode == "post_user_canceled":
    o = aw.seal(21, b"\x01\x5a") + reply(aw)
elif mode == "post_alert_trail":
    o = aw.seal(23, b"HTTP/1.0 200 ok\r\n") + aw.seal(21, b"\x01\x00\x00")
elif mode == "post_close_then_data":
    o = reply(aw) + aw.seal(23, b"EXTRA-AFTER-CLOSE")
elif mode == "post_appdata_max":
    body = b"HTTP/1.0 200 ok\r\ncontent-length: 16000\r\n\r\n"
    data = body + b"a" * (16384 - len(body))
    o = aw.seal(23, data) + aw.seal(21, b"\x01\x00")
elif mode == "post_appdata_over":
    body = b"HTTP/1.0 200 ok\r\ncontent-length: 16001\r\n\r\n"
    data = body + b"a" * (16385 - len(body))
    o = aw.seal(23, data) + aw.seal(21, b"\x01\x00")
elif mode == "post_empty_hs":
    o = aw.seal(22, b"") + reply(aw)
elif mode == "ccs_post":
    o = CCS + reply(aw)
elif mode == "post_hs_bigdecl":
    o = aw.seal(22, b"\x04\xff\xff\xff" + bytes(100)) + reply(aw)
else:
    o = reply(aw)
if mode in ("post_ku_flood", "post_close_then_data"):
    send(c, o)          # the flood is sized to be read whole; the data after close_notify must arrive
else:
    paced(o, quiet=False)
if tail:
    with cv_in:
        cv_in.wait_for(lambda: inbuf["eof"], timeout=3)
    if not inbuf["eof"]:
        send(c, tail)
if mode in ("post_trunc", "post_trunc_full"):
    try:
        c.shutdown(socket.SHUT_WR)
    except OSError:
        pass
# what the client sends afterwards: KeyUpdate answers, close_notify or an alert
client_records(on_app)
say(ku=st["ku"])
print("SRV " + " ".join("%s=%s" % kv for kv in facts.items()), flush=True)
c.close()
