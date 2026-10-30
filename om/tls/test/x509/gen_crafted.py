# SPDX-License-Identifier: Apache-2.0
# Test tooling only: write the crafted X.509 fixtures of the security review (omega-mv #813) into
# vectors/crafted/ with Python cryptography (>= 41). Keys are fresh random keys, so a rerun produces
# different (equally valid) fixtures; cases.txt does not depend on key bytes.
#   uid_*      repeated / reordered issuerUniqueID [1] and subjectUniqueID [2]   (finding 1)
#   *10001     SEQUENCE OF lists with 10001 elements, *9999 controls               (finding 2)
#   lXbig      a validly signed leaf larger than 16896 bytes                       (finding 2)
#   intXnc*    nameConstraints, NOT critical, on an intermediate                    (finding 5)
#   lX6144 lXe32 lXeven lXe1 intX6144 intXe32, controls lXe3 lX4096             (finding 6)
# Usage: python3 gen_crafted.py
import datetime
import math
import os
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "vectors", "crafted")
os.makedirs(out, exist_ok=True)
for f in os.listdir(out):
    os.remove(os.path.join(out, f))
T0 = datetime.datetime(2026, 1, 1)
T1 = datetime.datetime(2027, 1, 1)
R0 = datetime.datetime(2020, 1, 1)
R1 = datetime.datetime(2051, 1, 1)


def tlv(tag, body):
    n = len(body)
    if n < 128:
        h = bytes([n])
    else:
        b = n.to_bytes((n.bit_length() + 7) // 8, "big")
        h = bytes([0x80 | len(b)]) + b
    return bytes([tag]) + h + body


def parse(d):
    """children of the constructed TLV d as (tag, full TLV bytes)"""
    def hdr(d, i):
        t = d[i]
        n = d[i + 1]
        i += 2
        if n & 0x80:
            k = n & 0x7F
            n = int.from_bytes(d[i:i + k], "big")
            i += k
        return t, i, n
    _, i, n = hdr(d, 0)
    end = i + n
    r = []
    while i < end:
        s = i
        t, j, n = hdr(d, i)
        i = j + n
        r.append((t, d[s:i]))
    return r


def name(cn):
    return x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, "omtls test"),
                      x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def build(cn, pub, issuer, ikey, ca, t0=T0, t1=T1, san=("www.example.test", "example.test"), nc=None):
    b = (x509.CertificateBuilder().subject_name(name(cn)).issuer_name(issuer.subject if issuer else name(cn))
         .public_key(pub).serial_number(x509.random_serial_number()).not_valid_before(t0).not_valid_after(t1))
    b = b.add_extension(x509.SubjectKeyIdentifier.from_public_key(pub), critical=False)
    if issuer:
        b = b.add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ikey.public_key()), critical=False)
    if ca:
        b = b.add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        b = b.add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
        if nc:
            b = b.add_extension(x509.NameConstraints(permitted_subtrees=[x509.DNSName(nc)], excluded_subtrees=None),
                                critical=False)
    else:
        b = b.add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        b = b.add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), critical=True)
        b = b.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        b = b.add_extension(x509.SubjectAlternativeName([x509.DNSName(s) for s in san]), critical=False)
    return b.sign(ikey, hashes.SHA256())


def der(c):
    return c.public_bytes(serialization.Encoding.DER)


def put(n, data):
    with open(os.path.join(out, n), "wb") as f:
        f.write(data)


def resign(cdef, key, tbs_children):
    """certificate from replaced tbsCertificate children, signed again with key (same algorithm)"""
    alg = parse(cdef)[1][1]
    tbs = tlv(0x30, b"".join(tbs_children))
    if isinstance(key, ec.EllipticCurvePrivateKey):
        sig = key.sign(tbs, ec.ECDSA(hashes.SHA256()))
    else:
        sig = key.sign(tbs, padding.PKCS1v15(), hashes.SHA256())
    return tlv(0x30, tbs + alg + tlv(0x03, b"\x00" + sig))


def rsa_with_e(bits, e):
    """an RSA key with public exponent e (odd, coprime to lambda): CRT values from fresh primes"""
    while True:
        k = rsa.generate_private_key(public_exponent=65537, key_size=bits)
        p = k.private_numbers().p
        q = k.private_numbers().q
        lam = (p - 1) * (q - 1) // math.gcd(p - 1, q - 1)
        if math.gcd(e, lam) == 1:
            d = pow(e, -1, lam)
            pub = rsa.RSAPublicNumbers(e, p * q)
            return rsa.RSAPrivateNumbers(p, q, d, rsa.rsa_crt_dmp1(d, p), rsa.rsa_crt_dmq1(d, q),
                                         rsa.rsa_crt_iqmp(p, q), pub).private_key()


def rsa_spki(n, e):
    """SubjectPublicKeyInfo rsaEncryption with RSAPublicKey {n, e} (any e, no private key)"""
    def integer(v):
        b = v.to_bytes(v.bit_length() // 8 + 1, "big")
        return tlv(0x02, b)
    alg = tlv(0x30, tlv(0x06, bytes.fromhex("2a864886f70d010101")) + b"\x05\x00")
    return tlv(0x30, alg + tlv(0x03, b"\x00" + tlv(0x30, integer(n) + integer(e))))


# root (EC P-256) and store
rk = ec.generate_private_key(ec.SECP256R1())
root = build("rootX", rk.public_key(), None, rk, True, R0, R1)
put("rootX.der", der(root))
put("storeX.pem", root.public_bytes(serialization.Encoding.PEM))
lk = ec.generate_private_key(ec.SECP256R1())
base = der(build("lXbase", lk.public_key(), root, rk, False))
put("lXbase.der", base)
tbs = [c for _, c in parse(parse(base)[0][1])]
assert tbs[0][0] == 0xA0 and tbs[7][0] == 0xA3 and len(tbs) == 8
head, exts = tbs[:7], tbs[7]
uid1 = tlv(0x81, b"\x00\x01\x02")
uid2 = tlv(0x82, b"\x00\x03\x04")

# finding 1: unique IDs
for n, ids in [("uid_ok", [uid1, uid2]), ("uid_one2", [uid2]), ("uid_two1", [uid1, uid1]),
               ("uid_two2", [uid2, uid2]), ("uid_21", [uid2, uid1]),
               ("uid_five", [tlv(0x81, b"\x00")] * 5), ("uid_112", [uid1, uid1, uid2])]:
    put(n + ".der", resign(base, rk, head + ids + [exts]))


# finding 2: element counts (smallest valid elements, so the lists are short in bytes)
def ext(oid_hex, crit, value):
    return tlv(0x30, tlv(0x06, bytes.fromhex(oid_hex)) + (b"\x01\x01\xff" if crit else b"") + tlv(0x04, value))


def with_exts(extlist):
    return head + [tlv(0xA3, tlv(0x30, b"".join(extlist)))]


oid12 = tlv(0x06, b"\x2a")  # 1.2
for cnt in (9999, 10001):
    put("eku%d.der" % cnt, resign(base, rk, with_exts([ext("551d25", False, tlv(0x30, oid12 * cnt))])))
    put("san%d.der" % cnt, resign(base, rk, with_exts([ext("551d11", False, tlv(0x30, tlv(0x82, b"a") * cnt))])))
put("ext10001.der", resign(base, rk, with_exts([ext("2a03", False, b"")] * 10001)))
big = build("lXbig", lk.public_key(), root, rk, False,
            san=["www.example.test"] + ["n%04d.example.test" % i for i in range(1500)])
assert len(der(big)) > 16896
put("lXbig.der", der(big))

# finding 5: nameConstraints not critical on an intermediate
ik = ec.generate_private_key(ec.SECP256R1())
inc = build("intXnc", ik.public_key(), root, rk, True, nc="example.test")
put("intXnc.der", der(inc))
put("lViaXnc.der", der(build("lViaXnc", lk.public_key(), inc, ik, False)))
put("lViaXncOut.der", der(build("lViaXncOut", lk.public_key(), inc, ik, False, san=("www.other.test",))))

# finding 6: RSA policy for presented keys
k6144 = rsa.generate_private_key(public_exponent=65537, key_size=6144)
put("lX6144.der", der(build("lX6144", k6144.public_key(), root, rk, False)))
put("cv_x6144.sig", k6144.sign(open(os.path.join(here, "vectors", "pki", "cv.msg"), "rb").read(),
                                padding.PSS(padding.MGF1(hashes.SHA256()), 32), hashes.SHA256()))
k4096 = rsa.generate_private_key(public_exponent=65537, key_size=4096)
put("lX4096.der", der(build("lX4096", k4096.public_key(), root, rk, False)))
ke32 = rsa_with_e(2048, 4294967297)
put("lXe32.der", der(build("lXe32", ke32.public_key(), root, rk, False)))
ke3 = rsa.generate_private_key(public_exponent=3, key_size=2048)
put("lXe3.der", der(build("lXe3", ke3.public_key(), root, rk, False)))
n2048 = ke3.public_key().public_numbers().n
for nm, e in (("lXeven", 65536), ("lXe1", 1)):
    put(nm + ".der", resign(base, rk, tbs[:6] + [rsa_spki(n2048, e), exts]))
i6144 = rsa.generate_private_key(public_exponent=65537, key_size=6144)
ci = build("intX6144", i6144.public_key(), root, rk, True)
put("intX6144.der", der(ci))
put("lViaX6144.der", der(build("lViaX6144", lk.public_key(), ci, i6144, False)))
ie32 = rsa_with_e(2048, 4294967297)
ce = build("intXe32", ie32.public_key(), root, rk, True)
put("intXe32.der", der(ce))
put("lViaXe32.der", der(build("lViaXe32", lk.public_key(), ce, ie32, False)))
print(len(os.listdir(out)), "files in", out, file=sys.stderr)
