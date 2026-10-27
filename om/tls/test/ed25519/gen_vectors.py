# SPDX-License-Identifier: Apache-2.0
"""Write om/tls/test/ed25519/vectors/{rfc8032,wycheproof,edge}.txt (test tooling only).

Usage: python3 gen_vectors.py RFC8032_TXT WYCHEPROOF_CHECKOUT OUT_DIR
RFC8032_TXT is https://www.rfc-editor.org/rfc/rfc8032.txt; its section 7.1 (TEST 1, 2, 3, 1024,
SHA(abc)) is parsed. Every case becomes one driver line "V R PKHEX SIGHEX MSGHEX" ("-" for an empty
field) with R the return code ed25519_verify must give:
  2 public key length not 32 or the key does not decode (RFC 8032 section 5.1.3),
  3 signature length not 64 or S >= L, 1 any other failure, 0 valid.
R is computed by the RFC 8032 section 6 reference verification (cofactorless, transcribed below);
the script stops when that disagrees with an RFC vector or with a Wycheproof 'valid' / 'invalid'
result. Wycheproof 'acceptable' tests (small/mixed order keys) take the reference result.
"""
import hashlib
import json
import os
import re
import subprocess
import sys

# RFC 8032 section 6 reference arithmetic (Ed25519), transcribed
P = 2 ** 255 - 19
Q = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def point_add(a, b):
    A = (a[1] - a[0]) * (b[1] - b[0]) % P
    B = (a[1] + a[0]) * (b[1] + b[0]) % P
    C = 2 * a[3] * b[3] * D % P
    Dd = 2 * a[2] * b[2] % P
    E, F, G, H = B - A, Dd - C, Dd + C, B + A
    return (E * F % P, G * H % P, F * G % P, E * H % P)


def point_mul(s, a):
    q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            q = point_add(q, a)
        a = point_add(a, a)
        s >>= 1
    return q


def point_equal(a, b):
    return (a[0] * b[2] - b[0] * a[2]) % P == 0 and (a[1] * b[2] - b[1] * a[2]) % P == 0


def recover_x(y, sign):
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P)
    if x2 % P == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


def decompress(s):
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % P)


GY = 4 * pow(5, P - 2, P) % P
G = (recover_x(GY, 0), GY, 1, recover_x(GY, 0) * GY % P)


def expected(pk, sig, msg):
    """ed25519_verify's return code, from the RFC 8032 section 6 verify steps"""
    if len(pk) != 32:
        return 2
    a = decompress(pk)
    if a is None:
        return 2
    if len(sig) != 64:
        return 3
    s = int.from_bytes(sig[32:], "little")
    if s >= Q:
        return 3
    r = decompress(sig[:32])
    if r is None:
        return 1
    h = int.from_bytes(hashlib.sha512(sig[:32] + pk + msg).digest(), "little") % Q
    return 0 if point_equal(point_mul(s, G), point_add(r, point_mul(h, a))) else 1


LIMITS = (64, 256, 8192)  # driver.om buffers: public key, signature, message bytes


def line(rc, pk, sig, msg):
    for v, cap in zip((pk, sig, msg), LIMITS):
        if len(v) > cap:
            sys.exit("field of %d bytes exceeds the driver limit %d" % (len(v), cap))
    return "V %d %s %s %s" % (rc, pk.hex() or "-", sig.hex() or "-", msg.hex() or "-")


rfc_path, src, outdir = sys.argv[1], sys.argv[2], sys.argv[3]

# RFC 8032 section 7.1
raw = open(rfc_path, "rb").read()
text = raw.decode("ascii").splitlines()
start = text.index("7.1.  Test Vectors for Ed25519")
stop = text.index("7.2.  Test Vectors for Ed25519ctx")
tests = []
cur = None
label = None
for ln in text[start:stop]:
    m = re.match(r"^   -----TEST (.+)$", ln)
    if m:
        cur = {"name": m.group(1), "SECRET KEY": "", "PUBLIC KEY": "", "MESSAGE": "", "SIGNATURE": ""}
        tests.append(cur)
        label = None
        continue
    m = re.match(r"^   (SECRET KEY|PUBLIC KEY|SIGNATURE|MESSAGE)( \(length (\d+) bytes?\))?:$", ln)
    if m and cur is not None:
        label = m.group(1)
        if label == "MESSAGE":
            cur["mlen"] = int(m.group(3))
        continue
    m = re.match(r"^   ([0-9a-f]+)$", ln)
    if m and cur is not None and label is not None:
        cur[label] += m.group(1)
rfc_lines = []
for t in tests:
    pk, sig, msg = (bytes.fromhex(t[k]) for k in ("PUBLIC KEY", "SIGNATURE", "MESSAGE"))
    if len(msg) != t["mlen"] or len(pk) != 32 or len(sig) != 64:
        sys.exit("RFC TEST %s: field lengths wrong" % t["name"])
    if expected(pk, sig, msg) != 0:
        sys.exit("RFC TEST %s: reference rejects it" % t["name"])
    rfc_lines += ["# RFC 8032 section 7.1 TEST %s" % t["name"], line(0, pk, sig, msg)]
if [t["name"] for t in tests] != ["1", "2", "3", "1024", "SHA(abc)"]:
    sys.exit("RFC 8032 section 7.1: unexpected tests %s" % [t["name"] for t in tests])
head = [
    "# RFC 8032 Ed25519 test vectors, https://www.rfc-editor.org/rfc/rfc8032.txt (sha256",
    "# %s), section 7.1, all 5 tests, parsed by gen_vectors.py." % hashlib.sha256(raw).hexdigest(),
    "# License: rfc8032.NOTICE. Line format (read by driver.om):",
    "#   V R PKHEX SIGHEX MSGHEX   ed25519_verify(pk, m, sig) must return R; - is an empty field",
]
with open(os.path.join(outdir, "rfc8032.txt"), "w") as f:
    f.write("\n".join(head + rfc_lines) + "\n")

# Wycheproof ed25519_test.json
path = os.path.join(src, "testvectors_v1", "ed25519_test.json")
raw = open(path, "rb").read()
commit = subprocess.run(["git", "-C", src, "rev-parse", "HEAD"], capture_output=True, text=True,
                        check=True).stdout.strip()
doc = json.loads(raw)
lines = []
counts = {}
rcs = {}
for group in doc["testGroups"]:
    if group["type"] != "EddsaVerify" or group["publicKey"]["curve"] != "edwards25519":
        sys.exit("unexpected group %s" % group["type"])
    pk = bytes.fromhex(group["publicKey"]["pk"])
    for t in group["tests"]:
        sig, msg = bytes.fromhex(t["sig"]), bytes.fromhex(t["msg"])
        rc = expected(pk, sig, msg)
        res = t["result"]
        if (res == "valid" and rc != 0) or (res == "invalid" and rc == 0) or \
                res not in ("valid", "invalid", "acceptable"):
            sys.exit("tcId %d: %s but reference rc %d" % (t["tcId"], res, rc))
        counts[res] = counts.get(res, 0) + 1
        rcs[rc] = rcs.get(rc, 0) + 1
        lines.append("# tcId %d %s %s" % (t["tcId"], res, ",".join(t["flags"])))
        lines.append(line(rc, pk, sig, msg))
if doc["numberOfTests"] != sum(counts.values()):
    sys.exit("numberOfTests %d but %d tests read" % (doc["numberOfTests"], sum(counts.values())))
head = [
    "# Wycheproof ed25519_test.json, https://github.com/C2SP/wycheproof/blob/",
    "# %s/testvectors_v1/ed25519_test.json" % commit,
    "# sha256 %s, %d tests: %s." % (hashlib.sha256(raw).hexdigest(), doc["numberOfTests"],
                                    ", ".join("%s %d" % kv for kv in sorted(counts.items()))),
    "# Expected rc from the RFC 8032 section 6 reference verify in gen_vectors.py (agrees with every",
    "# valid/invalid result; acceptable tests take the cofactorless reference result): %s." %
    ", ".join("rc %d: %d" % kv for kv in sorted(rcs.items())),
    "# Copyright 2016-2026 The Wycheproof Authors, licensed under the Apache License 2.0",
    "# (wycheproof.LICENSE, upstream LICENSE verbatim). Modified: converted from JSON to this line",
    "# format by gen_vectors.py.",
]
with open(os.path.join(outdir, "wycheproof.txt"), "w") as f:
    f.write("\n".join(head + lines) + "\n")


def compress(a):
    zi = pow(a[2], P - 2, P)
    x, y = a[0] * zi % P, a[1] * zi % P
    return (y | ((x & 1) << 255)).to_bytes(32, "little")


# edge cases the published sets lack (no Wycheproof key fails to decode): each R is asserted to
# equal the reference result. With A the identity, (R, S) = ([5]B, 5) verifies for every message.
ident = (1).to_bytes(32, "little")
r5 = compress(point_mul(5, G))
sig5 = r5 + (5).to_bytes(32, "little")
noy = next(y for y in range(2, 100) if recover_x(y, 0) is None)
edge = [
    ("identity key, cofactorless accept", 0, ident, sig5, b"edge"),
    ("identity key, x = 0 with sign bit 1 (section 5.1.3 step 4)", 2,
     (1 + (1 << 255)).to_bytes(32, "little"), sig5, b"edge"),
    ("identity key encoded non-canonically, y = p + 1", 2, (P + 1).to_bytes(32, "little"), sig5,
     b"edge"),
    ("key y = %d not on the curve" % noy, 2, noy.to_bytes(32, "little"), sig5, b"edge"),
    ("key 31 bytes", 2, ident[:31], sig5, b"edge"),
    ("key 33 bytes", 2, ident + b"\0", sig5, b"edge"),
    ("key 31 bytes and signature 63 bytes: the key is checked first", 2, ident[:31], sig5[:63],
     b"edge"),
    ("signature 63 bytes", 3, ident, sig5[:63], b"edge"),
    ("signature 65 bytes", 3, ident, sig5 + b"\0", b"edge"),
    ("S = L", 3, ident, r5 + Q.to_bytes(32, "little"), b"edge"),
    ("S = L + 5 (the reduced S verifies)", 3, ident, r5 + (Q + 5).to_bytes(32, "little"), b"edge"),
    ("S = 2^256 - 1", 3, ident, r5 + b"\xff" * 32, b"edge"),
    ("R = [5]B with its sign bit flipped", 1, ident, r5[:31] + bytes([r5[31] ^ 128]) +
     (5).to_bytes(32, "little"), b"edge"),
]
elines = []
for name, rc, pk, sig, msg in edge:
    if expected(pk, sig, msg) != rc:
        sys.exit("edge %s: reference rc %d, listed %d" % (name, expected(pk, sig, msg), rc))
    elines += ["# " + name, line(rc, pk, sig, msg)]
head = [
    "# Ed25519 edge cases built by gen_vectors.py; each expected rc equals the RFC 8032 section 6",
    "# reference result computed there. Same line format as rfc8032.txt.",
]
with open(os.path.join(outdir, "edge.txt"), "w") as f:
    f.write("\n".join(head + elines) + "\n")
