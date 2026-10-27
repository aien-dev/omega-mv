# SPDX-License-Identifier: Apache-2.0
# Test tooling only: write the nameConstraints fixtures (RFC 5280 4.2.1.10) into vectors/nc/ with
# Python cryptography (>= 41). Keys are fresh random keys, so a rerun produces different (equally
# valid) fixtures; cases.txt does not depend on key bytes. Every constraint is critical (a
# non-critical one is X509_ERR_NAME_CONSTRAINTS by itself, see vectors/crafted/intXnc*).
#   storeN.pem   root rootN (no constraints) and root rootNC (permitted DNS other.test)
#   i*.der       intermediates under rootN, each with one constraint set, l*.der leaves under them
# Usage: python3 gen_nc.py
import datetime
import ipaddress
import os
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "vectors", "nc")
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


def name(cn, org="omtls test"):
    return x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, org),
                      x509.NameAttribute(NameOID.COMMON_NAME, cn)])


def dns(*names):
    return [x509.DNSName(s) for s in names]


class RawNC(x509.UnrecognizedExtension):
    """a nameConstraints extension with hand-written content (cryptography refuses the malformed ones)"""

    def __init__(self, value):
        super().__init__(x509.oid.ExtensionOID.NAME_CONSTRAINTS, value)


def build(cn, pub, issuer, ikey, ca, san=None, nc=None, rawnc=None, subject=None, t0=T0, t1=T1):
    subj = subject or name(cn)
    b = (x509.CertificateBuilder().subject_name(subj).issuer_name(issuer.subject if issuer else subj)
         .public_key(pub).serial_number(x509.random_serial_number()).not_valid_before(t0).not_valid_after(t1))
    b = b.add_extension(x509.SubjectKeyIdentifier.from_public_key(pub), critical=False)
    if issuer:
        b = b.add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ikey.public_key()), critical=False)
    if ca:
        b = b.add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        b = b.add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
    else:
        b = b.add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
        b = b.add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), critical=True)
        b = b.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        b = b.add_extension(x509.SubjectAlternativeName(san if san is not None else dns("www.example.test")),
                            critical=False)
    if nc:
        b = b.add_extension(x509.NameConstraints(permitted_subtrees=nc[0], excluded_subtrees=nc[1]), critical=True)
    if rawnc is not None:
        b = b.add_extension(RawNC(rawnc), critical=True)
    return b.sign(ikey, hashes.SHA256())


def der(c):
    return c.public_bytes(serialization.Encoding.DER)


def put(n, data):
    with open(os.path.join(out, n), "wb") as f:
        f.write(data)


def key():
    return ec.generate_private_key(ec.SECP256R1())


rk = key()
root = build("rootN", rk.public_key(), None, rk, True, t0=R0, t1=R1)
ck = key()
rootc = build("rootNC", ck.public_key(), None, ck, True, nc=(dns("other.test"), None), t0=R0, t1=R1)
ok = key()
rooto = build("rootNO", ok.public_key(), None, ok, True, nc=(dns("example.test"), None), t0=R0, t1=R1)
put("storeN.pem", b"".join(c.public_bytes(serialization.Encoding.PEM) for c in (root, rootc, rooto)))
lk = key()


def chain(tag, nc=None, rawnc=None, leaves=()):
    """intermediate i<tag> under rootN with the constraints, and leaves l<tag><suffix>"""
    ik = key()
    ic = build("i" + tag, ik.public_key(), root, rk, True, nc=nc, rawnc=rawnc)
    put("i" + tag + ".der", der(ic))
    for suf, kw in leaves:
        put("l" + tag + suf + ".der", der(build("l" + tag + suf, lk.public_key(), ic, ik, False, **kw)))
    return ic, ik


ip = x509.IPAddress
net = ipaddress.ip_network
# dNSName
chain("Dns", nc=(dns("example.test"), None), leaves=[
    ("Ok", {"san": dns("www.example.test", "example.test")}),
    ("Out", {"san": dns("www.example.test", "www.other.test")}),
    ("NoDot", {"san": dns("www.example.test", "badexample.test")}),
    ("Wild", {"san": dns("*.example.test")}),
])
chain("DnsX", nc=(None, dns("bad.example.test")), leaves=[
    ("Ok", {"san": dns("www.example.test")}),
    ("Hit", {"san": dns("www.example.test", "x.bad.example.test")}),
    ("Wild", {"san": dns("*.example.test")}),
])
# iPAddress
chain("Ip", nc=(None, [ip(net("192.0.2.0/24"))]), leaves=[
    ("Ok", {"san": dns("www.example.test") + [ip(ipaddress.ip_address("198.51.100.7"))]}),
    ("Hit", {"san": dns("www.example.test") + [ip(ipaddress.ip_address("192.0.2.5"))]}),
])
chain("IpP", nc=([ip(net("198.51.100.0/24"))], None), leaves=[
    ("DnsOnly", {"san": dns("www.example.test")}),
    ("Out", {"san": dns("www.example.test") + [ip(ipaddress.ip_address("192.0.2.5"))]}),
])
# rfc822Name
chain("Mail", nc=([x509.RFC822Name("example.test")], None), leaves=[
    ("Ok", {"san": dns("www.example.test") + [x509.RFC822Name("a@example.test")]}),
    ("Out", {"san": dns("www.example.test") + [x509.RFC822Name("a@other.test")]}),
])
# directoryName (the subject of every leaf here starts O=omtls test)
chain("Dn", nc=([x509.DirectoryName(x509.Name([x509.NameAttribute(NameOID.ORGANIZATION_NAME, "omtls test")]))], None),
      leaves=[("Ok", {}), ("Out", {"subject": name("lDnOut", "other org")})])
# a form that is never evaluated (URI): refused only when the certificate has a URI name, even one an
# excluded subtree would not match
chain("Uri", nc=(None, [x509.UniformResourceIdentifier("example.test")]), leaves=[
    ("NoUri", {}),
    ("Has", {"san": dns("www.example.test") + [x509.UniformResourceIdentifier("https://www.other.test/")]}),
])
# malformed / not evaluable constraints (hand-written content)
sub_dns = tlv(0x30, tlv(0x82, b"example.test"))
chain("Min", rawnc=tlv(0x30, tlv(0xA0, tlv(0x30, tlv(0x82, b"example.test") + tlv(0x80, b"\x01")))),
      leaves=[("", {})])
chain("Mask", rawnc=tlv(0x30, tlv(0xA1, tlv(0x30, tlv(0x87, bytes([192, 0, 2, 0, 255, 0, 255, 0]))))),
      leaves=[("", {})])
chain("Empty", rawnc=tlv(0x30, tlv(0xA0, b"") + tlv(0xA1, sub_dns)), leaves=[("", {})])
chain("None", rawnc=tlv(0x30, b""), leaves=[("", {})])
chain("Star", rawnc=tlv(0x30, tlv(0xA0, tlv(0x30, tlv(0x82, b"*.example.test")))), leaves=[("", {})])
# a leaf that carries nameConstraints itself
put("lLeafNc.der", der(build("lLeafNc", lk.public_key(), root, rk, False, nc=(dns("example.test"), None))))
# anchors with constraints: rootNC (other.test) refuses, rootNO (example.test) permits
put("lUnderNC.der", der(build("lUnderNC", lk.public_key(), rootc, ck, False)))
put("lUnderNO.der", der(build("lUnderNO", lk.public_key(), rooto, ok, False)))
# two constraining levels: rootNO (example.test) -> iNar (www.example.test) -> leaves
nk = key()
inar = build("iNar", nk.public_key(), rooto, ok, True, nc=(dns("www.example.test"), None))
put("iNar.der", der(inar))
put("lNarOk.der", der(build("lNarOk", lk.public_key(), inar, nk, False, san=dns("www.example.test"))))
put("lNarOut.der", der(build("lNarOut", lk.public_key(), inar, nk, False, san=dns("www.example.test", "a.example.test"))))
# an intermediate widening its parent: rootNO (example.test) -> iWide (other.test) -> leaf other.test
wk = key()
iwide = build("iWide", wk.public_key(), rooto, ok, True, nc=(dns("other.test"), None))
put("iWide.der", der(iwide))
put("lWide.der", der(build("lWide", lk.public_key(), iwide, wk, False, san=dns("www.other.test"))))
print(len(os.listdir(out)), "files in", out, file=sys.stderr)
