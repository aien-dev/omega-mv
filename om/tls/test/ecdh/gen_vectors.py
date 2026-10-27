#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only (an oracle and a format converter, not part of the library): converts the NIST
# CAVP ECC CDH primitive vectors and the Wycheproof ECDH files into the flat vector files read by
# om/tls/test/ecdh/driver.om, and writes the edge-case file. A plain-integer reference (affine
# double-and-add, variable time, test-only) recomputes every expected value; a source vector that
# disagrees with it stops the conversion.
#
# Usage: python3 gen_vectors.py WYCHEPROOF_DIR CAVP_ZIP OUT_DIR
#
# Line format (space separated, '#' starts a comment line, '-' is an empty hex field):
#   ID OP RC DHEX PHEX XHEX
# OP S ecdh_shared (X = shared secret, zeros when RC != 0), P ecdh_pub (P '-', X = 04||x||y),
# C ecdh_pct (P = public key to check, X '-'). RC is the exact return code.
import hashlib
import json
import os
import sys
import zipfile

CURVES = {
    256: dict(
        name="secp256r1", cavp="P-256",
        p=0xFFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF,
        n=0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551,
        b=0x5AC635D8AA3A93E7B3EBBD55769886BC651D06B0CC53B0F63BCE3C3E27D2604B,
        gx=0x6B17D1F2E12C4247F8BCE6E563A440F277037D812DEB33A0F4A13945D898C296,
        gy=0x4FE342E2FE1A7F9B8EE7EB4A7C0F9E162BCE33576B315ECECBB6406837BF51F5,
        spki="3059301306072a8648ce3d020106082a8648ce3d030107034200"),
    384: dict(
        name="secp384r1", cavp="P-384",
        p=0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFF0000000000000000FFFFFFFF,
        n=0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973,
        b=0xB3312FA7E23EE7E4988E056BE3F82D19181D9C6EFE8141120314088F5013875AC656398D8A2ED19D2A85C8EDD3EC2AEF,
        gx=0xAA87CA22BE8B05378EB1C71EF320AD746E1D3B628BA79B9859F741E082542A385502F25DBF55296C3A545E3872760AB7,
        gy=0x3617DE4A96262C6F5D9E98BF9292DC29F8F41DBD289A147CE9DA3113B5F0B8C00A60B1CE1D7E819D7A431D7C90EA0E5F,
        spki="3076301006072a8648ce3d020106052b81040022036200"),
}
NB = {256: 32, 384: 48}


def on_curve(c, x, y):
    k = CURVES[c]
    return x < k["p"] and y < k["p"] and (y * y - x ** 3 + 3 * x - k["b"]) % k["p"] == 0


def padd(c, a, b):
    p = CURVES[c]["p"]
    if a is None:
        return b
    if b is None:
        return a
    if a[0] == b[0]:
        if (a[1] + b[1]) % p == 0:
            return None
        lam = (3 * a[0] * a[0] - 3) * pow(2 * a[1], p - 2, p) % p
    else:
        lam = (b[1] - a[1]) * pow(b[0] - a[0], p - 2, p) % p
    x = (lam * lam - a[0] - b[0]) % p
    return (x, (lam * (a[0] - x) - a[1]) % p)


def smul(c, k, pt):
    r = None
    for bit in bin(k)[2:] if k else "":
        r = padd(c, r, r)
        if bit == "1":
            r = padd(c, r, pt)
    return r


def enc(c, pt):
    nb = NB[c]
    return "04" + pt[0].to_bytes(nb, "big").hex() + pt[1].to_bytes(nb, "big").hex()


def dec(c, h):
    nb = NB[c]
    b = bytes.fromhex(h)
    if len(b) != 1 + 2 * nb or b[0] != 4:
        return None
    x, y = int.from_bytes(b[1:1 + nb], "big"), int.from_bytes(b[1 + nb:], "big")
    return (x, y) if on_curve(c, x, y) else None


def shared_ref(c, d, ph):
    """(rc, z hex) the library must give for ecdh_shared(d, P)"""
    nb = NB[c]
    pt = dec(c, ph)
    if pt is None:
        return 3, "00" * nb
    if not 1 <= d < CURVES[c]["n"]:
        return 4, "00" * nb
    r = smul(c, d, pt)
    if r is None:
        return 1, "00" * nb
    return 0, r[0].to_bytes(nb, "big").hex()


def pub_ref(c, d):
    nb = NB[c]
    if not 1 <= d < CURVES[c]["n"]:
        return 4, "00" * (1 + 2 * nb)
    k = CURVES[c]
    return 0, enc(c, smul(c, d, (k["gx"], k["gy"])))


def dhex(c, d):
    return d.to_bytes(NB[c], "big").hex()


def cavp(zpath, out):
    raw = zipfile.ZipFile(zpath).read("KAS_ECC_CDH_PrimitiveTest.txt").decode()
    lines, cur, rec, n = [], None, {}, 0
    for ln in raw.splitlines():
        ln = ln.strip()
        if ln.startswith("["):
            cur = {"[P-256]": 256, "[P-384]": 384}.get(ln)
            continue
        if cur is None or "=" not in ln:
            continue
        k, v = [s.strip() for s in ln.split("=", 1)]
        rec[k] = v
        if k == "ZIUT":
            c, nb = cur, NB[cur]
            d = int(rec["dIUT"], 16)
            peer = "04" + rec["QCAVSx"].zfill(2 * nb) + rec["QCAVSy"].zfill(2 * nb)
            q = "04" + rec["QIUTx"].zfill(2 * nb) + rec["QIUTy"].zfill(2 * nb)
            z = rec["ZIUT"].zfill(2 * nb)
            if shared_ref(c, d, peer) != (0, z) or pub_ref(c, d) != (0, q):
                sys.exit("cavp %s COUNT %s disagrees with the reference" % (c, rec["COUNT"]))
            lines.append("cavp-%d-%s S 0 %s %s %s" % (c, rec["COUNT"], dhex(c, d), peer, z))
            lines.append("cavp-%d-%s-pub P 0 %s - %s" % (c, rec["COUNT"], dhex(c, d), q))
            n += 1
            rec = {}
    with open(os.path.join(out, "cavp_cdh.txt"), "w") as f:
        f.write("# NIST CAVP KAS_ECC_CDH_PrimitiveTest.txt [P-256] and [P-384] (see NOTICE.cavp)\n")
        f.write("\n".join(lines) + "\n")
    return n


def wy_ecpoint(wdir, c, out):
    path = os.path.join(wdir, "ecdh_%s_ecpoint_test.json" % CURVES[c]["name"])
    doc = json.load(open(path))
    lines = []
    for g in doc["testGroups"]:
        for t in g["tests"]:
            d = int(t["private"], 16)
            rc, z = shared_ref(c, d, t["public"])
            if t["result"] == "valid":
                if rc != 0 or z != t["shared"]:
                    sys.exit("%s tcId %d valid but the reference gives %d" % (path, t["tcId"], rc))
            elif rc == 0:
                if "CompressedPoint" not in t["flags"] and "CompressedPublic" not in t["flags"]:
                    sys.exit("%s tcId %d %s but the reference accepts it" % (path, t["tcId"], t["result"]))
            lines.append("wy-%s-ecpoint-%d S %d %s %s %s" % (CURVES[c]["name"], t["tcId"], rc, dhex(c, d),
                                                            t["public"] or "-", z))
    name = "wycheproof_%s_ecpoint.txt" % CURVES[c]["name"]
    with open(os.path.join(out, name), "w") as f:
        f.write("# Wycheproof %s (sha256 %s)\n" % (os.path.basename(path), sha(path)))
        f.write("# valid: the shared secret; invalid and compressed (acceptable): refused with 3, since a\n")
        f.write("# TLS 1.3 key_share for secp256r1/secp384r1 is the uncompressed form only (RFC 8446 4.2.8.2)\n")
        f.write("\n".join(lines) + "\n")
    return len(lines)


def wy_asn(wdir, c, out, skipped):
    """the SPKI-encoded file: cases whose key is the standard named-curve SPKI for this curve give the
    raw point; every other case is counted per flag in skipped (the SPKI/ASN.1 layer is not in this
    unit: a TLS key_share carries the raw point)"""
    path = os.path.join(wdir, "ecdh_%s_test.json" % CURVES[c]["name"])
    pre = CURVES[c]["spki"]
    lines = []
    for g in json.load(open(path))["testGroups"]:
        for t in g["tests"]:
            pk = t["public"]
            if not pk.startswith(pre) or len(pk) != len(pre) + 2 * (1 + 2 * NB[c]):
                key = "%s %s" % (os.path.basename(path), ",".join(sorted(t["flags"])) or "none")
                skipped[key] = skipped.get(key, 0) + 1
                continue
            d = int(t["private"], 16)
            pt = pk[len(pre):]
            rc, z = shared_ref(c, d, pt)
            if t["result"] == "valid" and (rc != 0 or z != t["shared"]):
                sys.exit("%s tcId %d valid but the reference gives %d" % (path, t["tcId"], rc))
            if t["result"] == "invalid" and rc == 0:
                sys.exit("%s tcId %d invalid but the reference accepts it" % (path, t["tcId"]))
            lines.append("wy-%s-asn-%d S %d %s %s %s" % (CURVES[c]["name"], t["tcId"], rc, dhex(c, d), pt, z))
    name = "wycheproof_%s_spki.txt" % CURVES[c]["name"]
    with open(os.path.join(out, name), "w") as f:
        f.write("# Wycheproof %s (sha256 %s): named-curve SPKI cases, raw point extracted\n"
                % (os.path.basename(path), sha(path)))
        f.write("\n".join(lines) + "\n")
    return len(lines)


def edge(out):
    lines = []
    for c in (256, 384):
        k, nb = CURVES[c], NB[c]
        n, p = k["n"], k["p"]
        g = (k["gx"], k["gy"])
        G = enc(c, g)
        s = "edge-%d-" % c
        for tag, d in (("d0", 0), ("d1", 1), ("d2", 2), ("dn-1", n - 1), ("dn", n), ("dn+1", n + 1),
                       ("dmax", 2 ** (8 * nb) - 1), ("dhigh", 2 ** (8 * nb - 1))):
            rc, q = pub_ref(c, d)
            lines.append("%spub-%s P %d %s - %s" % (s, tag, rc, dhex(c, d), q))
            rc, z = shared_ref(c, d, G)
            lines.append("%sshared-%s S %d %s %s %s" % (s, tag, rc, dhex(c, d), G, z))
        d = 0x1234567 * 7919 + 3
        Q = pub_ref(c, d)[1]
        bad = [("infinity00", "00"), ("compressed", "02" + G[2:2 + 2 * nb]),
               ("hybrid", "06" + G[2:]), ("prefix05", "05" + G[2:]),
               ("x-eq-p", "04" + p.to_bytes(nb, "big").hex() + G[2 + 2 * nb:]),
               ("y-eq-p", "04" + G[2:2 + 2 * nb] + p.to_bytes(nb, "big").hex()),
               ("off-curve", G[:-2] + "%02x" % (int(G[-2:], 16) ^ 1)),
               ("zero-point", "04" + "00" * (2 * nb)), ("short", G[:-2]), ("long", G + "00"),
               ("empty", "-")]
        for tag, pt in bad:
            lines.append("%speer-%s S 3 %s %s %s" % (s, tag, dhex(c, d), pt, "00" * nb))
        # -G is valid: the ladder handles the inverse of the base point
        ng = enc(c, (g[0], p - g[1]))
        rc, z = shared_ref(c, d, ng)
        lines.append("%speer-minusG S %d %s %s %s" % (s, rc, dhex(c, d), ng, z))
        # length codes: z one byte long (2), scalar one byte short (5) and long (5)
        lines.append("%szlen S 2 %s %s %s" % (s, dhex(c, d), G, "00" * (nb + 1)))
        lines.append("%sdshort S 5 %s %s %s" % (s, dhex(c, d)[2:], G, "00" * (nb - 1)))
        lines.append("%sdlong S 5 00%s %s %s" % (s, dhex(c, d), G, "00" * (nb + 1)))
        lines.append("%sqlen P 2 %s - %s" % (s, dhex(c, d), "00" * (2 * nb)))
        # pair-wise consistency
        lines.append("%spct-ok C 0 %s %s -" % (s, dhex(c, d), Q))
        lines.append("%spct-otherkey C 6 %s %s -" % (s, dhex(c, d + 1), Q))
        lines.append("%spct-offcurve C 6 %s %s -" % (s, dhex(c, d), Q[:-2] + "%02x" % (int(Q[-2:], 16) ^ 1)))
        lines.append("%spct-d0 C 6 %s %s -" % (s, dhex(c, 0), Q))
        lines.append("%spct-len C 2 %s %s -" % (s, dhex(c, d), Q[:-2]))
    with open(os.path.join(out, "edge.txt"), "w") as f:
        f.write("# edge cases from the reference in gen_vectors.py: scalar range (0, n-1, n, n+1, 2^8nb-1),\n")
        f.write("# peer encodings refused (infinity 00, compressed, hybrid, coordinate = p, off curve), -G,\n")
        f.write("# buffer lengths, pair-wise consistency\n")
        f.write("\n".join(lines) + "\n")
    return len(lines)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    wdir, zpath, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    counts = {"cavp": cavp(zpath, out)}
    skipped = {}
    for c in (256, 384):
        counts["ecpoint-%d" % c] = wy_ecpoint(wdir, c, out)
        counts["spki-%d" % c] = wy_asn(wdir, c, out, skipped)
    counts["edge"] = edge(out)
    with open(os.path.join(out, "NOT_RUN.txt"), "w") as f:
        f.write("# Wycheproof d17cc89f ecdh_* cases not converted, by file and flag set. Reason: the key is\n")
        f.write("# not the standard named-curve SPKI of the file's curve (other curve OID, explicit or\n")
        f.write("# missing parameters, compressed point or malformed ASN.1); the SPKI/ASN.1 layer is not in\n")
        f.write("# ecdh.om, whose input is the raw TLS key_share point. Other Wycheproof ecdh files (other\n")
        f.write("# curves, pem, webcrypto) are not converted: the approved groups are secp256r1 and secp384r1.\n")
        for k in sorted(skipped):
            f.write("NOT_RUN wycheproof %s: %d\n" % (k, skipped[k]))
    print(" ".join("%s=%d" % kv for kv in sorted(counts.items())))


if __name__ == "__main__":
    main()
