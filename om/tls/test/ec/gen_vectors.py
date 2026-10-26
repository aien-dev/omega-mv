#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only (no primitive is implemented here): converts the Wycheproof ECDSA JSON files and
# the NIST CAVP 186-4 SigVer.rsp into the flat vector files read by om/tls/test/ec/driver.om.
# hashlib computes the reference digest column, which the driver cross-checks against its own
# sha2_digest (om/tls/sha2.om) before every verify.
#
# Usage: python3 gen_vectors.py WYCHEPROOF_DIR CAVP_ZIP OUT_DIR
#
# Line format (space separated, '#' starts a comment line, '-' is an empty hex field):
#   ID CRV MODE EXP ALG PUBHEX SIGHEX MSGHEX DIGESTHEX
# CRV 256|384; MODE d (DER) or r (raw r||s, IEEE P1363); EXP 0 valid / 1 must be refused;
# ALG 256|384|512 (SHA-2 digest of MSG).
import hashlib
import json
import os
import subprocess
import sys
import zipfile

WY_FILES = [
    "ecdsa_secp256r1_sha256_test.json",
    "ecdsa_secp256r1_sha256_p1363_test.json",
    "ecdsa_secp256r1_sha512_test.json",
    "ecdsa_secp256r1_sha512_p1363_test.json",
    "ecdsa_secp384r1_sha256_test.json",
    "ecdsa_secp384r1_sha384_test.json",
    "ecdsa_secp384r1_sha384_p1363_test.json",
    "ecdsa_secp384r1_sha512_test.json",
    "ecdsa_secp384r1_sha512_p1363_test.json",
]
CURVES = {"secp256r1": 256, "secp384r1": 384, "P-256": 256, "P-384": 384}
SHAS = {"SHA-256": 256, "SHA-384": 384, "SHA-512": 512}
CAVP_URL = ("https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/"
            "documents/dss/186-4ecdsatestvectors.zip")


def digest(alg, msg):
    return {256: hashlib.sha256, 384: hashlib.sha384, 512: hashlib.sha512}[alg](msg).hexdigest()


def hx(s):
    return s if s else "-"


def der(r, s):
    # minimal DER Ecdsa-Sig-Value (both parts positive), so every CAVP case also runs through the
    # DER entry point
    def integer(v):
        b = v.to_bytes(max(1, (v.bit_length() + 8) // 8), "big")
        return b"\x02" + bytes([len(b)]) + b
    body = integer(r) + integer(s)
    return (b"\x30" + bytes([len(body)]) + body).hex()


def sha256_file(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def wycheproof(wdir, name, out):
    path = os.path.join(wdir, "testvectors_v1", name)
    d = json.load(open(path))
    commit = subprocess.run(["git", "-C", wdir, "rev-parse", "HEAD"], capture_output=True,
                            text=True, check=True).stdout.strip()
    tag = name[len("ecdsa_"):-len("_test.json")]
    lines = [
        "# source: https://github.com/C2SP/wycheproof testvectors_v1/" + name,
        "# commit: " + commit + " sha256: " + sha256_file(path),
        "# license: Apache-2.0 (see LICENSE.wycheproof beside this file)",
        "# numberOfTests: %d" % d["numberOfTests"],
    ]
    count = {"valid": 0, "invalid": 0}
    for g in d["testGroups"]:
        crv = CURVES[g["publicKey"]["curve"]]
        alg = SHAS[g["sha"]]
        mode = {"EcdsaVerify": "d", "EcdsaP1363Verify": "r"}[g["type"]]
        pub = g["publicKey"]["uncompressed"]
        for t in g["tests"]:
            res = t["result"]
            if res not in count:
                raise SystemExit("%s tcId %d: result %s not handled" % (name, t["tcId"], res))
            count[res] += 1
            exp = 0 if res == "valid" else 1
            msg = bytes.fromhex(t["msg"])
            lines.append("w-%s-%d %d %s %d %d %s %s %s %s" % (
                tag, t["tcId"], crv, mode, exp, alg, pub, hx(t["sig"]), hx(t["msg"]),
                digest(alg, msg)))
    if count["valid"] + count["invalid"] != d["numberOfTests"]:
        raise SystemExit(name + ": test count disagrees with numberOfTests")
    lines.insert(4, "# valid %d invalid %d acceptable 0" % (count["valid"], count["invalid"]))
    with open(os.path.join(out, "wycheproof_" + tag + ".txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


def cavp(zpath, out):
    with zipfile.ZipFile(zpath) as z:
        text = z.read("186-4ecdsatestvectors/SigVer.rsp").decode()
    lines = [
        "# source: " + CAVP_URL + " member 186-4ecdsatestvectors/SigVer.rsp",
        "# zip sha256: " + sha256_file(zpath),
        "# license: NIST CAVP test data, a work of the U.S. Government (see NOTICE.cavp)",
        "# sections P-256 and P-384 with SHA-256, SHA-384, SHA-512 (other sections NOT_RUN)",
    ]
    sec = None
    cur = {}
    n = {"P": 0, "F": 0}
    idx = 0
    for raw in text.splitlines():
        s = raw.strip()
        if s.startswith("["):
            c, h = s[1:-1].split(",")
            sec = (CURVES[c], SHAS[h]) if c in CURVES and h in SHAS else None
            idx = 0
            continue
        if sec is None or "=" not in s:
            continue
        k, v = [x.strip() for x in s.split("=", 1)]
        cur[k] = v
        if k != "Result":
            continue
        crv, alg = sec
        idx += 1
        w = crv // 4
        pub = "04" + cur["Qx"].rjust(w, "0") + cur["Qy"].rjust(w, "0")
        sig = cur["R"].rjust(w, "0") + cur["S"].rjust(w, "0")
        r = v[0]
        n[r] += 2
        msg = bytes.fromhex(cur["Msg"])
        for mode, sg in (("r", sig), ("d", der(int(cur["R"], 16), int(cur["S"], 16)))):
            lines.append("c-p%d-sha%d-%d%s %d %s %d %d %s %s %s %s" % (
                crv, alg, idx, mode, crv, mode, 0 if r == "P" else 1, alg, pub, sg, cur["Msg"],
                digest(alg, msg)))
        cur = {}
    lines.insert(4, "# lines: P (valid) %d F (invalid) %d (each case twice: raw r||s and DER)" % (n["P"], n["F"]))
    with open(os.path.join(out, "cavp_sigver.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


P = {256: int("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff", 16),
     384: int("fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffe"
              "ffffffff0000000000000000ffffffff", 16)}
N = {256: int("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551", 16),
     384: int("ffffffffffffffffffffffffffffffffffffffffffffffffc7634d81f4372ddf"
              "581a0db248b0a77aecec196accc52973", 16)}


def edge(wdir, out):
    # Hand-made encoding cases with an exact expected code (EXP "=K"), each derived from a valid
    # DER test of a Wycheproof file. Constants p, n: FIPS 186-4 D.1.2.3 / D.1.2.4.
    lines = ["# edge cases derived from Wycheproof (Apache-2.0, see LICENSE.wycheproof); EXP =K means",
             "# the exact return code K is required (0 valid, 2 encoding, 3 key, 4 range, 5 curve)"]
    for crv, name in ((256, "ecdsa_secp256r1_sha256_test.json"), (384, "ecdsa_secp384r1_sha384_test.json")):
        d = json.load(open(os.path.join(wdir, "testvectors_v1", name)))
        g = d["testGroups"][0]
        # first valid test whose r INTEGER is exactly crv/8 bytes with top byte < 0x80, so that a
        # single 0x00 pad (der-int-pad) is a non-minimal encoding of a value that would verify
        t = next(t for t in g["tests"] if t["result"] == "valid" and
                 int(t["sig"][6:8], 16) == crv // 8 and int(t["sig"][8:10], 16) < 0x80)
        alg = SHAS[g["sha"]]
        pub = g["publicKey"]["uncompressed"]
        sig = t["sig"]
        msg = bytes.fromhex(t["msg"])
        dg = digest(alg, msg)
        w = crv // 4
        x, y = int(pub[2:2 + w], 16), int(pub[2 + w:], 16)
        b = bytes.fromhex(sig)
        rl = b[3]
        r = int.from_bytes(b[4:4 + rl], "big")
        s = int.from_bytes(b[6 + rl:], "big")
        raw = r.to_bytes(crv // 8, "big").hex() + s.to_bytes(crv // 8, "big").hex()
        h = lambda v: v.to_bytes(crv // 8, "big").hex()
        cases = [
            ("valid", crv, "d", "=0", pub, sig),
            ("valid-raw", crv, "r", "=0", pub, raw),
            ("crv521", 521, "d", "=5", pub, sig),
            ("crv0", 0, "r", "=5", pub, raw),
            ("other-crv", 384 if crv == 256 else 256, "d", "=3", pub, sig),
            ("pub-compressed", crv, "d", "=3", "02" + pub[2:2 + w], sig),
            ("pub-prefix", crv, "d", "=3", "06" + pub[2:], sig),
            ("pub-short", crv, "d", "=3", pub[:-2], sig),
            ("pub-long", crv, "d", "=3", pub + "00", sig),
            ("pub-x-eq-p", crv, "d", "=3", "04" + h(P[crv]) + h(y), sig),
            ("pub-y-eq-p", crv, "d", "=3", "04" + h(x) + h(P[crv]), sig),
            ("pub-y+p-wraps", crv, "d", "=3", "04" + h(x) + h((y + P[crv]) % (1 << crv)), sig),
            ("pub-off-curve", crv, "d", "=3", "04" + h(x) + h(y ^ 1), sig),
            ("pub-zero", crv, "d", "=3", "04" + h(0) + h(0), sig),
            ("der-trailing", crv, "d", "=2", pub, sig + "00"),
            ("der-empty", crv, "d", "=2", pub, ""),
            ("der-long-len", crv, "d", "=2", pub, "3081" + sig[2:]),
            ("der-int-pad", crv, "d", "=2", pub, der_raw(b"\x00" + r.to_bytes(crv // 8, "big"), s)),
            ("der-neg-r", crv, "d", "=2", pub, der_raw(b"\x80" + bytes(crv // 8 - 1), s)),
            ("der-tag", crv, "d", "=2", pub, "31" + sig[2:]),
            ("raw-short", crv, "r", "=2", pub, raw[:-2]),
            ("raw-long", crv, "r", "=2", pub, raw + "00"),
            ("r-zero", crv, "r", "=4", pub, h(0) + h(s)),
            ("s-zero", crv, "d", "=4", pub, der(r, 0)),
            ("r-eq-n", crv, "d", "=4", pub, der(N[crv], s)),
            ("s-eq-n", crv, "r", "=4", pub, h(r) + h(N[crv])),
            ("r-too-long", crv, "d", "=4", pub, der_raw(b"\x01" + bytes(crv // 8), s)),
            ("s-max", crv, "r", "=4", pub, h(r) + "ff" * (crv // 8)),
        ]
        for cid, c, mode, exp, pk, sg in cases:
            lines.append("e-p%d-%s %d %s %s %d %s %s %s %s" % (crv, cid, c, mode, exp, alg, hx(pk), hx(sg),
                                                             hx(t["msg"]), dg))
    with open(os.path.join(out, "edge.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


def der_raw(rb, s):
    # DER-shaped signature whose r INTEGER content is rb verbatim (for non-minimal / negative r)
    sb = s.to_bytes(max(1, (s.bit_length() + 8) // 8), "big")
    body = b"\x02" + bytes([len(rb)]) + rb + b"\x02" + bytes([len(sb)]) + sb
    return (b"\x30" + bytes([len(body)]) + body).hex()


def main():
    wdir, zpath, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    for name in WY_FILES:
        wycheproof(wdir, name, out)
    cavp(zpath, out)
    edge(wdir, out)


if __name__ == "__main__":
    main()
