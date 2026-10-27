#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only (om/tls fuzz targets, run.sh): writes the seed corpus of every fuzz target from
# the repo's own vectors, plus a TLS 1.3 server flight built with python3 `cryptography` for the
# deterministic ClientHello of fz_flight. Nothing here is the implementation under test.
# usage: gen_seeds.py OUTDIR CHFILE
#   CHFILE: the ClientHello fz_flight writes with argument 2 (run.sh does this first)
# Writes OUTDIR/<target>/<name> and OUTDIR/fzstore.pem (pki/storeAll.pem + this run's own root).
# Keys are fresh on every run, so the flight seeds differ byte-wise between runs (run.sh keeps the
# committed seeds/ directory; regenerate only together with fzstore.pem).
import datetime
import hashlib
import hmac
import importlib.util
import os
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, x25519
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

HERE = os.path.dirname(os.path.abspath(__file__))
TEST = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(os.path.dirname(TEST)))
PKI = os.path.join(TEST, "x509", "vectors", "pki")
HOST = b"www.example.test"
NOW = 1792972800


def u8(n): return bytes([n])
def u16(n): return n.to_bytes(2, "big")
def u24(n): return n.to_bytes(3, "big")
def msg(t, body): return u8(t) + u24(len(body)) + body
def chunk(t, b): return u8(t) + u16(len(b)) + b
def rec(t, b): return u8(t) + b"\x03\x03" + u16(len(b)) + b
def fill(n, v): return bytes((v + i) & 255 for i in range(n))
def h256(b): return hashlib.sha256(b).digest()
def rd(*p): return open(os.path.join(*p), "rb").read()


def expand_label(secret, label, ctx, n):
    info = u16(n) + u8(6 + len(label)) + b"tls13 " + label + u8(len(ctx)) + ctx
    out, t, i = b"", b"", 1
    while len(out) < n:
        t = hmac.new(secret, t + info + u8(i), hashlib.sha256).digest()
        out += t
        i += 1
    return out[:n]


def extract(salt, ikm): return hmac.new(salt, ikm, hashlib.sha256).digest()


def pki():
    """root -> int -> leaf, P-256, valid 2025..2049, leaf SAN www.example.test; SKI on root and
    int, AKI on int and leaf (x509.om chain_leaf / chain_ca, RFC 5280 4.2.1.1-2, since #833)"""
    nb = datetime.datetime(2025, 1, 1, tzinfo=datetime.timezone.utc)
    na = datetime.datetime(2049, 12, 31, tzinfo=datetime.timezone.utc)
    keys = [ec.generate_private_key(ec.SECP256R1()) for _ in range(3)]
    names = [x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, n)])
             for n in ("omfz root", "omfz int", "www.example.test")]
    certs = []
    for i in range(3):
        b = (x509.CertificateBuilder().subject_name(names[i])
             .issuer_name(names[max(i - 1, 0)]).public_key(keys[i].public_key())
             .serial_number(1000 + i).not_valid_before(nb).not_valid_after(na))
        ca = i < 2
        b = b.add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True)
        b = b.add_extension(x509.KeyUsage(
            digital_signature=not ca, content_commitment=False, key_encipherment=False,
            data_encipherment=False, key_agreement=False, key_cert_sign=ca, crl_sign=ca,
            encipher_only=False, decipher_only=False), critical=True)
        if ca:
            b = b.add_extension(x509.SubjectKeyIdentifier.from_public_key(keys[i].public_key()), False)
        if i > 0:
            b = b.add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(
                keys[i - 1].public_key()), False)
        if not ca:
            b = b.add_extension(x509.SubjectAlternativeName([x509.DNSName(HOST.decode())]), False)
            b = b.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), False)
        certs.append(b.sign(keys[max(i - 1, 0)], hashes.SHA256()))
    der = [c.public_bytes(serialization.Encoding.DER) for c in certs]
    pem = certs[0].public_bytes(serialization.Encoding.PEM)
    return der, keys[2], pem


def entry(d): return u24(len(d)) + d + b"\x00\x00"


def cvsig(key, th):
    content = b"\x20" * 64 + b"TLS 1.3, server CertificateVerify\x00" + th
    return key.sign(content, ec.ECDSA(hashes.SHA256())), content


class Out:
    def __init__(self, root):
        self.root = root
        self.n = 0

    def put(self, target, name, data):
        d = os.path.join(self.root, target)
        os.makedirs(d, exist_ok=True)
        open(os.path.join(d, name), "wb").write(data)
        self.n += 1


def flight(o, ch, der, key, suite):
    """fz_flight: R raw chunks for the ClientHello ch (tls_mkch made deterministic by fz_detch)"""
    sk = x25519.X25519PrivateKey.from_private_bytes(fill(32, 7))
    spub = sk.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    cpub = ch[ch.index(b"\x00\x1d\x00\x20") + 4:][:32]
    shared = sk.exchange(x25519.X25519PublicKey.from_public_bytes(cpub))
    sid = ch[39:71]
    ext = (u16(43) + u16(2) + b"\x03\x04" + u16(51) + u16(36) + u16(29) + u16(32) + spub)
    sh = msg(2, b"\x03\x03" + fill(32, 150) + u8(32) + sid + u16(suite) + b"\x00"
             + u16(len(ext)) + ext)
    z = b"\x00" * 32
    early = extract(z, z)
    hs = extract(expand_label(early, b"derived", h256(b""), 32), shared)
    shts = expand_label(hs, b"s hs traffic", h256(ch + sh), 32)
    alpn = u16(16) + u16(11) + u16(9) + u8(8) + b"http/1.1"
    ee = msg(8, u16(len(alpn)) + alpn)
    lst = b"".join(entry(d) for d in (der[2], der[1]))
    cert = msg(11, b"\x00" + u24(len(lst)) + lst)
    sig, _ = cvsig(key, h256(ch + sh + ee + cert))
    cv = msg(15, u16(0x0403) + u16(len(sig)) + sig)
    fk = expand_label(shts, b"finished", b"", 32)
    fin = msg(20, hmac.new(fk, h256(ch + sh + ee + cert + cv), hashlib.sha256).digest())
    raw = rec(22, sh) + rec(20, b"\x01")
    tag = "chacha" if suite == 0x1303 else "aes"
    pre = u16(len(raw)) + raw
    o.put("fz_flight", f"full_{tag}", pre + chunk(22, ee + cert + cv + fin))
    o.put("fz_flight", f"split_{tag}", pre + b"".join(chunk(22, m) for m in (ee, cert, cv, fin)))
    frag = ee + cert + cv + fin
    o.put("fz_flight", f"frag_{tag}", pre + b"".join(chunk(22, frag[i:i + 300])
                                                     for i in range(0, len(frag), 300)))
    cr = msg(13, b"\x00" + u16(8) + u16(13) + u16(4) + u16(2) + u16(0x0403))
    o.put("fz_flight", f"cr_{tag}", pre + chunk(22, ee + cr + cert + cv + fin))
    o.put("fz_flight", f"pad_{tag}", pre + chunk(22, ee) + chunk(0, b"") + chunk(22, cert + cv + fin))
    o.put("fz_flight", f"alert_{tag}", pre + chunk(22, ee) + chunk(21, b"\x02\x28"))
    o.put("fz_flight", f"shonly_{tag}", u16(len(rec(22, sh))) + rec(22, sh))
    # HelloRetryRequest: a cookie-only request changes ClientHello2 and is accepted; naming x25519,
    # whose share ClientHello1 already carries, is illegal_parameter (RFC 8446 4.1.4, TLEKEYS)
    for name, ext in (("hrr", u16(44) + u16(6) + u16(4) + b"omfz"), ("hrrdup", u16(51) + u16(2) + u16(29))):
        hrr = msg(2, b"\x03\x03" + h256(b"HelloRetryRequest") + u8(32) + sid + u16(suite) + b"\x00"
                  + u16(6 + len(ext)) + u16(43) + u16(2) + b"\x03\x04" + ext)
        o.put("fz_flight", f"{name}_{tag}", u16(len(rec(22, hrr))) + rec(22, hrr))
    return sh, ee, cert, cv


def hs(o, der, key):
    """fz_hs: S message, against fixed state (TLSID zero, TLSHS 33.., transcript hash 217..)"""
    spub = fill(32, 9)
    ext = (u16(43) + u16(2) + b"\x03\x04" + u16(51) + u16(36) + u16(29) + u16(32) + spub)
    for suite in (0x1301, 0x1303):
        o.put("fz_hs", f"sh_{suite:04x}", u8(0) + msg(2, b"\x03\x03" + fill(32, 1) + u8(32)
                                                         + b"\x00" * 32 + u16(suite) + b"\x00"
                                                         + u16(len(ext)) + ext))
    v = {}
    for line in open(os.path.join(TEST, "tls", "vectors", "rfc8448-1rtt.txt")):
        p = line.split()
        if len(p) == 2 and not line.startswith("#"):
            v[p[0]] = bytes.fromhex(p[1]) if all(c in "0123456789abcdef" for c in p[1]) else None
    o.put("fz_hs", "sh_rfc8448", u8(0) + v["sh"])
    o.put("fz_hs", "ee_rfc8448", u8(1) + v["ee"])
    alpn = u16(16) + u16(11) + u16(9) + u8(8) + b"http/1.1"
    o.put("fz_hs", "ee_alpn", u8(1) + msg(8, u16(len(alpn)) + alpn))
    o.put("fz_hs", "cr", u8(2) + msg(13, b"\x00" + u16(8) + u16(13) + u16(4) + u16(2) + u16(0x0403)))
    lst = b"".join(entry(d) for d in (der[2], der[1]))
    o.put("fz_hs", "cert_own", u8(3) + msg(11, b"\x00" + u24(len(lst)) + lst))
    o.put("fz_hs", "cert_rfc8448", u8(3) + v["cert"])
    sig, _ = cvsig(key, fill(32, 217))
    le = entry(der[2])
    o.put("fz_hs", "cv_own", u8(4) + u16(len(le)) + le + msg(15, u16(0x0403) + u16(len(sig)) + sig))
    fk = expand_label(fill(32, 33), b"finished", b"", 32)
    o.put("fz_hs", "sfin", u8(5) + msg(20, hmac.new(fk, fill(32, 217), hashlib.sha256).digest()))
    o.put("fz_hs", "kupd0", u8(6) + msg(24, b"\x00"))
    o.put("fz_hs", "kupd1", u8(6) + msg(24, b"\x01"))
    nst = msg(4, fill(4, 0) + fill(4, 4) + u8(8) + fill(8, 8) + u16(16) + fill(16, 16) + u16(0))
    o.put("fz_hs", "post_nst", u8(7) + nst)
    o.put("fz_hs", "post_nst2_ku", u8(7) + nst + nst + msg(24, b"\x00"))
    o.put("fz_hs", "post_split", u8(7) + nst[:5])


def x509s(o, der):
    """fz_x509: M rest (0 parse, 1 chain, 2 time+host+chain, 3 CertificateVerify)"""
    for sub in ("pki", "real", "crafted"):
        d = os.path.join(TEST, "x509", "vectors", sub)
        for f in sorted(os.listdir(d)):
            if f.endswith(".der") and os.path.getsize(os.path.join(d, f)) <= 8192:
                o.put("fz_x509", f"p_{sub}_{f[:-4]}", u8(0) + rd(d, f))
    own = b"".join(entry(d) for d in (der[2], der[1]))
    rsa = entry(rd(PKI, "lR.der")) + entry(rd(PKI, "intR.der"))
    o.put("fz_x509", "c_own", u8(1) + own)
    o.put("fz_x509", "c_rsa", u8(1) + rsa)
    for name, now, host in (("hq_own", NOW, HOST), ("hq_late", 4102444800, HOST),
                            ("hq_other", NOW, b"other.example.test")):
        o.put("fz_x509", name, u8(2) + now.to_bytes(8, "big")
              + u8(len(host)) + host + own)
    for leaf, sch, sig in (("lE256", 0x0403, "cv_e256"), ("lE384", 0x0503, "cv_e384"),
                           ("lR", 0x0804, "cv_r256"), ("lPSS384", 0x0805, "cv_r384"),
                           ("lPSS512", 0x0806, "cv_r512")):
        e = entry(rd(PKI, leaf + ".der"))
        o.put("fz_x509", f"cv_{leaf}", u8(3) + u16(sch) + u16(len(e)) + e
              + rd(PKI, "cv.msg") + rd(PKI, sig + ".sig"))


def pems(o):
    """fz_pem: a PEM bundle (argument 1 = a certificate_list file)"""
    for f in ("storeAll.pem", "storeR.pem", "storeE.pem", "storeBad.pem", "storeDup.pem",
              "storeInt.pem", "storeX.pem"):
        o.put("fz_pem", f[:-4], rd(PKI, f))


def dns(o):
    """fz_dns: M rest; M even: H name[H] response (dns_parse of dns_query id 0x4f4d), odd: resolv.conf"""
    for i, line in enumerate(open(os.path.join(TEST, "tls", "vectors", "dns.txt"))):
        p = line.split()
        if len(p) == 4 and p[0] in ("0", "E"):
            h = p[2].encode()
            o.put("fz_dns", f"r{i:02d}", u8(0) + u8(min(len(h), 255)) + h[:255] + bytes.fromhex(p[3]))
        elif len(p) == 2 and p[0] == "L":
            o.put("fz_dns", f"l{i:02d}", u8(0) + u8(min(len(p[1]), 255)) + p[1].encode()[:255])
    for i, t in enumerate((b"nameserver 192.0.2.53\n",
                           b"# c\nsearch example.test\nnameserver 10.0.0.1\nnameserver ::1\n",
                           b"options ndots:1\nnameserver\t127.0.0.53 \n",
                           b"nameserver 256.1.1.1\nnameserver 1.2.3\n")):
        o.put("fz_dns", f"conf{i}", u8(1) + t)


def http(o):
    """fz_http: M message (0 response, 1 response to HEAD, 2 request); check.py's hand cases"""
    spec = importlib.util.spec_from_file_location("check", os.path.join(REPO, "seed", "net",
                                                                        "tests", "check.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    for i, (mode, b, _) in enumerate(m.hand_cases()):
        o.put("fz_http", f"hand{i:02d}", u8({"s": 0, "h": 1, "q": 2}[mode]) + b)


def https(o):
    """fz_https: M chunks (M bit 0 HEAD, bit 1 AES), established keys, sealed by the mirror"""
    r1 = b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello"
    r2 = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n"
    r3 = b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.0 200 OK\r\n\r\nbody to close"
    close = chunk(21, b"\x01\x00")
    nst = msg(4, fill(4, 0) + fill(4, 4) + u8(8) + fill(8, 8) + u16(16) + fill(16, 16) + u16(0))
    for m in (0, 2):
        tag = "aes" if m else "chacha"
        o.put("fz_https", f"cl_{tag}", u8(m) + chunk(23, r1) + close)
        o.put("fz_https", f"chunked_{tag}", u8(m) + chunk(23, r2[:20]) + chunk(23, r2[20:]) + close)
        o.put("fz_https", f"interim_{tag}", u8(m) + chunk(23, r3) + close)
        o.put("fz_https", f"nst_ku_{tag}", u8(m) + chunk(22, nst) + chunk(22, msg(24, b"\x01"))
              + chunk(23, r1) + close)
        o.put("fz_https", f"pad_{tag}", u8(m) + chunk(23, b"") + chunk(23, r1) + close)
        o.put("fz_https", f"trunc_{tag}", u8(m) + chunk(23, r1[:30]))
        o.put("fz_https", f"raw_{tag}", u8(m) + chunk(128, rec(23, b"\x00" * 20)))
    o.put("fz_https", "head", u8(1) + chunk(23, r1[:-5]) + close)


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: gen_seeds.py OUTDIR CHFILE")
    o = Out(sys.argv[1])
    ch = open(sys.argv[2], "rb").read()
    der, key, root = pki()
    open(os.path.join(sys.argv[1], "fzstore.pem"), "wb").write(rd(PKI, "storeAll.pem") + root)
    flight(o, ch, der, key, 0x1303)
    flight(o, ch, der, key, 0x1301)
    hs(o, der, key)
    x509s(o, der)
    pems(o)
    o.put("fz_pem", "own_root", root)
    open(os.path.join(sys.argv[1], "chain.bin"), "wb").write(
        b"".join(entry(d) for d in (der[2], der[1])))
    dns(o)
    o.put("fz_plant", "plain", u8(0) + b"abcd")  # no byte of "OMFZ": the fuzzer climbs all four
    http(o)
    https(o)
    print(f"gen_seeds: {o.n} seeds in {sys.argv[1]}")


if __name__ == "__main__":
    main()
