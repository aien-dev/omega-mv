# SPDX-License-Identifier: Apache-2.0
"""Mozilla partial-distrust table for om/tls/x509.om (generator, not shipped).

usage: python3 gen_distrust.py CERTDATA_TXT [BUNDLE_PEM]
Reads Mozilla NSS certdata.txt and prints, for every root with CKA_NSS_SERVER_DISTRUST_AFTER, one
line "SHA256(root DER) UNIX_SECONDS LABEL": a leaf whose notBefore is later than UNIX_SECONDS must not
chain to that root (Mozilla root store policy 7.4 / NSS behaviour). The hex pairs go into x509.om
X5DIST. With BUNDLE_PEM it also checks the bundle against certdata: every bundle root must be a
CKO_NSS_TRUST object with CKA_TRUST_SERVER_AUTH CKT_NSS_TRUSTED_DELEGATOR, and it reports which
partially distrusted roots the bundle carries. Exit 1 on a bundle root that is not a trusted
server-auth delegator.
"""
import base64
import calendar
import hashlib
import re
import sys


def octal(lines):
    out = bytearray()
    for ln in lines:
        for m in re.findall(r"\\([0-7]{3})", ln):
            out.append(int(m, 8))
    return bytes(out)


def objects(path):
    obj, cur, ml = {}, None, None
    for ln in open(path, encoding="utf-8"):
        ln = ln.rstrip("\n")
        if ml is not None:
            if ln == "END":
                obj[cur] = octal(ml)
                ml = None
            else:
                ml.append(ln)
            continue
        if ln.startswith("CKA_CLASS"):
            if obj:
                yield obj
            obj = {}
        if not ln.startswith("CKA_"):
            continue
        f = ln.split(None, 2)
        if len(f) >= 2 and f[1] == "MULTILINE_OCTAL":
            cur, ml = f[0], []
        elif len(f) == 3:
            obj[f[0]] = f[2].strip('"') if f[1] == "UTF8" else f[2]
    if obj:
        yield obj


def utc(b):
    s = b.decode()
    if len(s) == 13:  # YYMMDDHHMMSSZ
        y = int(s[:2])
        s = ("20" if y < 50 else "19") + s
    return calendar.timegm((int(s[:4]), int(s[4:6]), int(s[6:8]), int(s[8:10]), int(s[10:12]), int(s[12:14])))


def main():
    certs, trust = {}, {}
    for o in objects(sys.argv[1]):
        if o.get("CKA_CLASS") == "CKO_CERTIFICATE" and "CKA_VALUE" in o:
            certs[hashlib.sha1(o["CKA_VALUE"]).digest()] = o
        elif o.get("CKA_CLASS") == "CKO_NSS_TRUST" and "CKA_CERT_SHA1_HASH" in o:
            trust[o["CKA_CERT_SHA1_HASH"]] = o
    rows = {}
    for h, o in certs.items():
        k = hashlib.sha256(o["CKA_VALUE"]).hexdigest()
        lab = o.get("CKA_LABEL", "?")
        t = trust.get(h, {}).get("CKA_TRUST_SERVER_AUTH")
        d = o.get("CKA_NSS_SERVER_DISTRUST_AFTER")
        if t != "CKT_NSS_TRUSTED_DELEGATOR":
            rows[k] = (0, "not a Mozilla server-auth root: " + lab)
        elif isinstance(d, bytes) and d:
            rows[k] = (utc(d), "distrust after: " + lab)
    bad = 0
    if len(sys.argv) >= 3:
        pem = open(sys.argv[2]).read()
        ders = [base64.b64decode(b) for b in re.findall(r"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", pem, re.S)]
        n = 0
        for der in ders:
            k = hashlib.sha256(der).hexdigest()
            if hashlib.sha1(der).digest() not in certs:
                rows[k] = (0, "bundle root absent from certdata (removed by Mozilla)")
            if k in rows:
                n += 1
                print("BUNDLE DISTRUSTED %s %d %s" % (k, rows[k][0], rows[k][1]))
        print("BUNDLE roots=%d distrusted=%d" % (len(ders), n))
    for k in sorted(rows):
        print("%s %010d %s" % (k, rows[k][0], rows[k][1]))
    print('text X5DIST "%s"' % "".join("%s %010d " % (k, rows[k][0]) for k in sorted(rows)))
    return bad


sys.exit(main())
