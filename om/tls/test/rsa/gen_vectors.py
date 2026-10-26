#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling for om/tls/rsa.om: converts Wycheproof RSA signature JSON to the flat vector format
# read by driver.om, writes the hand-built edge file, and (with --pyca) the pyca/cryptography
# differential file. No RSA arithmetic of the library is reimplemented here except as an oracle
# for the --pyca vectors (signatures come from pyca/cryptography; one large-exponent key is
# assembled from pyca primes with Python pow, a test oracle only).
#
# Usage: python3 gen_vectors.py WYCHEPROOF_DIR [--pyca]
#
# Line format (space separated, one case per line, '#' starts a comment line):
#   EXP MODE CTL KEY MSG DIG SIG ID
#   EXP  0..5 exact expected return code, or n = any nonzero (refused)
#   MODE p = rsa_verify_pkcs1 (CTL = hash bits), s = rsa_verify_pss (CTL = packed ctl, decimal)
#   KEY  DER RSAPublicKey hex; MSG message hex ('-' empty, '*' = no message: use DIG verbatim)
#   DIG  hashlib digest of MSG with the CTL hash (cross-checked against sha2_digest by the driver)
#   SIG  signature hex ('-' empty); ID source reference, ignored by the driver
import hashlib
import json
import os
import subprocess
import sys

HASHES = {"SHA-256": 256, "SHA-384": 384, "SHA-512": 512}
HL = {256: hashlib.sha256, 384: hashlib.sha384, 512: hashlib.sha512}
# acceptable flags and the way rsa.om goes (documented in run.sh output and the README section)
ACCEPT_WAY = {
    "MissingNull": "n",  # DigestInfo without NULL: rsa.om compares the full DER block -> refused
    "SmallModulus": "0",  # 1024/1536-bit keys: accepted by the primitive; >=2048 is caller policy
    "SmallPublicKey": "0",  # e = 3: accepted (odd, >= 3)
    "WeakHash": None,  # only on SHA-1/SHA-224 groups, which are NOT_RUN
}

here = os.path.dirname(os.path.abspath(__file__))
vdir = os.path.join(here, "vectors")


def hx(b):
    return b.hex() if b else "-"


def ctl(h, m, s):
    return h | (m << 16) | (s << 32)


def line(exp, mode, c, key, msg, dig, sig, ident):
    return f"{exp} {mode} {c} {key} {msg} {dig} {sig} {ident}\n"


def dig(h, msg):
    return HL[h](msg).hexdigest()


def wy_header(src, sha):
    return (f"# source: https://github.com/C2SP/wycheproof testvectors_v1/{src}\n"
            f"# commit: {COMMIT}\n# file sha256: {sha}\n"
            "# license: Apache-2.0 (vectors/LICENSE.wycheproof)\n"
            "# converted by om/tls/test/rsa/gen_vectors.py\n")


def scope_of(fname):
    """('pkcs1'|'pss'|None, reason) for a testvectors_v1/rsa_* file name."""
    if fname.startswith("rsa_signature_"):
        return "pkcs1", None
    if fname.startswith("rsa_pkcs1_") and fname.endswith("_sig_gen_test.json"):
        return "pkcs1", None
    if fname.startswith("rsa_pss_"):
        return "pss", None
    if "oaep" in fname:
        return None, "RSAES-OAEP encryption/decryption suite: not in scope (signature verification only)"
    if fname.startswith("rsa_pkcs1_"):
        return None, "RSAES-PKCS1-v1_5 encryption/decryption suite: not in scope (signature verification only)"
    return None, "not an RSA signature verification suite"


def hash_reason(sha, mgf):
    bad = sha if sha not in HASHES else mgf
    name = {"SHA-1": "SHA-1", "SHA-224": "SHA-224", "SHA-512/224": "SHA-512/224",
            "SHA-512/256": "SHA-512/256"}.get(bad, bad)
    if bad and bad.startswith("SHA3"):
        return f"{bad}: SHA-3 not in scope (sha2.om provides SHA-256/384/512 only)"
    if bad and bad.startswith("SHAKE"):
        return f"{bad}: SHAKE not in scope"
    return f"{name}: hash not in scope (SHA-256/384/512 only)"


def wycheproof(wdir):
    tv = os.path.join(wdir, "testvectors_v1")
    os.makedirs(os.path.join(vdir, "wycheproof"), exist_ok=True)
    notrun = []
    stats = {}
    for f in sorted(os.listdir(tv)):
        if not f.startswith("rsa_") or not f.endswith(".json"):
            continue
        kind, why = scope_of(f)
        raw = open(os.path.join(tv, f), "rb").read()
        d = json.loads(raw)
        if kind is None:
            notrun.append(f"{f} tests={d['numberOfTests']} reason: {why}\n")
            continue
        out = []
        skipped = {}
        for g in d["testGroups"]:
            sha = g["sha"]
            mgf = g.get("mgfSha", sha) if kind == "pss" else sha
            if kind == "pss" and g.get("mgf", "MGF1") != "MGF1":
                skipped.setdefault(f"mgf {g.get('mgf')}", 0)
                skipped[f"mgf {g.get('mgf')}"] += len(g["tests"])
                continue
            if sha not in HASHES or mgf not in HASHES:
                r = hash_reason(sha, mgf) if kind == "pkcs1" else \
                    f"hash {sha} mgf1 {mgf}: " + hash_reason(sha, mgf)
                skipped[r] = skipped.get(r, 0) + len(g["tests"])
                continue
            key = g.get("publicKeyAsn") or g.get("keyAsn")
            h = HASHES[sha]
            c = h if kind == "pkcs1" else ctl(h, HASHES[mgf], g["sLen"])
            mode = "p" if kind == "pkcs1" else "s"
            for t in g["tests"]:
                res = t["result"]
                if res == "valid":
                    exp = "0"
                elif res == "invalid":
                    exp = "n"
                else:
                    ways = {ACCEPT_WAY[fl] for fl in t["flags"] if ACCEPT_WAY.get(fl) is not None}
                    assert len(ways) == 1, (f, t["tcId"], t["flags"])
                    exp = ways.pop()
                msg = bytes.fromhex(t["msg"])
                ident = f"{f}#{t['tcId']}/{res}"
                if res == "acceptable":
                    ident += "/" + "+".join(t["flags"])
                out.append(line(exp, mode, c, key, hx(msg), dig(h, msg), t["sig"] or "-", ident))
        sha = hashlib.sha256(raw).hexdigest()
        if out:
            name = f[:-5] + ".txt"
            with open(os.path.join(vdir, "wycheproof", name), "w") as o:
                o.write(wy_header(f, sha))
                for r, n in sorted(skipped.items()):
                    o.write(f"# NOT_RUN groups of this file: {n} tests, {r}\n")
                o.writelines(out)
            stats[f] = len(out)
        for r, n in sorted(skipped.items()):
            notrun.append(f"{f} tests={n} reason: {r}\n")
    with open(os.path.join(vdir, "NOT_RUN.txt"), "w") as o:
        o.write(f"# Wycheproof {COMMIT} testvectors_v1/rsa_* tests not run by om/tls/rsa.om\n")
        o.writelines(notrun)
    return stats


def der_len(n):
    if n < 0x80:
        return bytes([n])
    if n < 0x100:
        return bytes([0x81, n])
    return bytes([0x82, n >> 8, n & 0xff])


def der(tag, c, lenbytes=None):
    return bytes([tag]) + (der_len(len(c)) if lenbytes is None else lenbytes) + c


def dint(v):
    b = v.to_bytes(max(1, (v.bit_length() + 8) // 8), "big")
    return b


def rsakey(n_c, e_c):
    return der(0x30, der(0x02, n_c) + der(0x02, e_c))


def edge(wdir):
    """Exact-code cases built from Wycheproof rsa_pss_2048_sha256_mgf1_32 group 1 (n, e = 65537)."""
    src = "rsa_pss_2048_sha256_mgf1_32_test.json"
    d = json.load(open(os.path.join(wdir, "testvectors_v1", src)))
    g = d["testGroups"][0]
    key = bytes.fromhex(g["publicKeyAsn"])
    t = d["testGroups"][0]["tests"][0]
    assert t["result"] == "valid"
    msg = bytes.fromhex(t["msg"])
    sig = bytes.fromhex(t["sig"])
    n = int(g["publicKey"]["modulus"], 16)
    e = int(g["publicKey"]["publicExponent"], 16)
    k = (n.bit_length() + 7) // 8
    c32 = ctl(256, 256, 32)
    dg = dig(256, msg)
    ncb, ecb = dint(n), dint(e)
    cases = []

    def add(exp, mode, c, kk, s, ident, m=msg, dd=None):
        cases.append(line(exp, mode, c, hx(kk), hx(m) if m != "*" else "*",
                          dd if dd is not None else dig(c & 0xffff, m), hx(s), "edge#" + ident))

    add("0", "s", c32, key, sig, "baseline-valid")
    # 5: hash
    add("5", "s", ctl(256, 1, 32), key, sig, "mgf1-hash-sha1-code")
    add("5", "s", ctl(224, 256, 32), key, sig, "hash-224", m="*", dd="00" * 28)
    add("5", "s", ctl(256, 256, 32), key, sig, "hlen-48-for-sha256", m="*", dd="00" * 48)
    add("5", "p", 160, key, sig, "pkcs1-hash-160", m="*", dd="00" * 20)
    add("5", "p", 0, key, sig, "pkcs1-hash-0", m="*", dd="00" * 32)
    add("5", "p", 256, key, sig, "pkcs1-hlen-64-for-sha256", m="*", dd="00" * 64)
    # 2: key encoding
    inner = der(0x02, ncb) + der(0x02, ecb)
    add("2", "s", c32, b"", sig, "key-empty")
    add("2", "s", c32, b"\x31" + key[1:], sig, "key-outer-set-tag")
    add("2", "s", c32, key + b"\x00", sig, "key-trailing-byte")
    add("2", "s", c32, key[:-1], sig, "key-truncated")
    add("2", "s", c32, b"\x30\x83\x00" + key[2:], sig, "key-len-3-octets-nonminimal")
    add("2", "s", c32, b"\x30\x80" + inner + b"\x00\x00", sig, "key-indefinite-length")
    add("2", "s", c32, der(0x30, inner + der(0x02, b"\x01")), sig, "key-extra-integer")
    add("2", "s", c32, der(0x30, der(0x02, ncb[1:]) + der(0x02, ecb)), sig, "key-n-negative")
    add("2", "s", c32, der(0x30, der(0x02, b"\x00" + ncb) + der(0x02, ecb)), sig, "key-n-leading-zero")
    add("2", "s", c32, der(0x30, der(0x02, ncb) + der(0x02, b"\x00" + ecb)), sig, "key-e-leading-zero")
    add("2", "s", c32, der(0x30, der(0x03, ncb) + der(0x02, ecb)), sig, "key-n-bitstring-tag")
    add("2", "s", c32, der(0x30, der(0x02, ncb) + b"\x02\x81\x03" + ecb), sig, "key-e-len-81-short")
    add("2", "s", c32, der(0x30, der(0x02, ncb) + b"\x02\x00"), sig, "key-e-empty-integer")
    add("2", "s", c32, b"\x30\x82\x01\x0b" + inner, sig, "key-outer-len-too-long")
    add("2", "s", c32, b"\x30\x82\x00\xff" + inner, sig, "key-outer-len-82-nonminimal")
    add("2", "s", c32, b"\x30", sig, "key-tag-only")
    add("2", "p", 256, b"\x30\x82\x01", sig, "key-cut-length")
    add("2", "s", c32, der(0x30, der(0x02, ncb) + der(0x02, ecb)[:-1]), sig, "key-e-cut")
    # 3: size / exponent
    add("3", "s", c32, rsakey(ncb, dint(1)), sig, "e-1")
    add("3", "s", c32, rsakey(ncb, dint(2)), sig, "e-2")
    add("3", "s", c32, rsakey(ncb, dint(0)), sig, "e-0")
    add("3", "s", c32, rsakey(ncb, dint(65536)), sig, "e-even")
    add("3", "s", c32, rsakey(dint(n ^ 1), ecb), sig, "n-even")
    add("3", "s", c32, rsakey(dint(n >> 1025), ecb), sig, "n-1023-bits")
    add("3", "s", c32, rsakey(dint((n << 6145) | 1), ecb), sig, "n-8193-bits")
    add("3", "s", c32, rsakey(ncb, dint((1 << 2048) + 1)), sig, "e-2049-bits-gt-n")
    add("3", "p", 256, rsakey(dint(0), ecb), sig, "n-0")
    # 4: signature length / range
    add("4", "s", c32, key, sig[1:], "sig-short")
    add("4", "s", c32, key, b"\x00" + sig, "sig-leading-zero-long")
    add("4", "s", c32, key, b"", "sig-empty")
    add("4", "s", c32, key, n.to_bytes(k, "big"), "sig-eq-n")
    add("4", "p", 256, key, (n + 1).to_bytes(k, "big"), "sig-n-plus-1")
    add("4", "s", c32, key, b"\xff" * k, "sig-all-ff")
    add("1", "s", c32, key, (n - 1).to_bytes(k, "big"), "sig-n-minus-1")
    add("1", "s", c32, key, b"\x00" * k, "sig-zero")
    add("1", "p", 256, key, (1).to_bytes(k, "big"), "sig-one")
    # 1: PSS parameter mismatch on the valid signature
    add("1", "s", ctl(256, 256, 31), key, sig, "pss-salt-31")
    add("1", "s", ctl(256, 256, 0), key, sig, "pss-salt-0")
    add("1", "s", ctl(256, 256, 0xffffffff), key, sig, "pss-salt-huge")
    add("1", "s", ctl(256, 384, 32), key, sig, "pss-mgf-sha384")
    add("1", "p", 256, key, sig, "pss-sig-as-pkcs1")
    with open(os.path.join(vdir, "edge.txt"), "w") as o:
        o.write("# exact-code edge cases, built by gen_vectors.py from Wycheproof "
                f"testvectors_v1/{src} group 1 test 1 (commit {COMMIT}, Apache-2.0)\n")
        o.writelines(cases)
    return len(cases)


def pyca():
    """Odd modulus sizes and a large public exponent, signed by pyca/cryptography (test oracle)."""
    import cryptography
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import padding, rsa
    H = {256: hashes.SHA256, 384: hashes.SHA384, 512: hashes.SHA512}
    out = []
    keys = []
    for bits, e in ((1025, 65537), (1031, 3), (2047, 65537), (2049, 65537), (2056, 3),
                    (3073, 65537), (4095, 65537), (4097, 3), (8192, 65537)):
        keys.append((f"{bits}-e{e}", rsa.generate_private_key(e, bits)))
    # a 2048-bit key with a 2047-bit odd public exponent, assembled from pyca primes
    base = rsa.generate_private_key(65537, 2048).private_numbers()
    p, q = base.p, base.q
    lam = (p - 1) * (q - 1)
    import math
    import secrets
    while True:
        e = secrets.randbits(2047) | (1 << 2046) | 1
        if math.gcd(e, lam) == 1:
            break
    dd = pow(e, -1, lam)
    pn = rsa.RSAPublicNumbers(e, p * q)
    big = rsa.RSAPrivateNumbers(p, q, dd, dd % (p - 1), dd % (q - 1), pow(q, -1, p), pn).private_key()
    keys.append(("2048-e2047bits", big))
    for name, sk in keys:
        pk = sk.public_key()
        kder = pk.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.PKCS1)
        hlen_max = {256: 32, 384: 48, 512: 64}
        for h in (256, 384, 512):
            for msg in (b"", b"om/tls rsa " + name.encode()):
                s = sk.sign(msg, padding.PKCS1v15(), H[h]())
                out.append(line("0", "p", h, kder.hex(), hx(msg), dig(h, msg), s.hex(),
                                f"pyca#{name}/pkcs1/sha{h}"))
            emlen = (pk.key_size - 1 + 7) // 8
            for m in (256, 384, 512):
                for sl in sorted({0, hlen_max[h], emlen - hlen_max[h] - 2}):
                    if sl < 0 or emlen < hlen_max[h] + sl + 2:
                        continue
                    msg = b"pss " + name.encode()
                    s = sk.sign(msg, padding.PSS(padding.MGF1(H[m]()), sl), H[h]())
                    out.append(line("0", "s", ctl(h, m, sl), kder.hex(), hx(msg), dig(h, msg),
                                    s.hex(), f"pyca#{name}/pss/sha{h}/mgf{m}/salt{sl}"))
    with open(os.path.join(vdir, "pyca_gen.txt"), "w") as o:
        o.write(f"# differential vectors signed by pyca/cryptography {cryptography.__version__} "
                "(random keys; regenerate with gen_vectors.py --pyca; committed output is the test)\n"
                "# sizes 1025 1031 2047 2049 2056 3073 4095 4097 8192 bits, e = 3 / 65537, and one "
                "2048-bit key with a 2047-bit e\n")
        o.writelines(out)
    return len(out)


if __name__ == "__main__":
    wdir = sys.argv[1]
    COMMIT = subprocess.run(["git", "-C", wdir, "rev-parse", "HEAD"], capture_output=True,
                            text=True, check=True).stdout.strip()
    os.makedirs(vdir, exist_ok=True)
    st = wycheproof(wdir)
    print("wycheproof", sum(st.values()), "cases in", len(st), "files")
    print("edge", edge(wdir))
    if "--pyca" in sys.argv:
        print("pyca", pyca())
