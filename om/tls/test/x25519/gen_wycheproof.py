# SPDX-License-Identifier: Apache-2.0
"""Convert Wycheproof x25519_test.json to om/tls/test/x25519/vectors/wycheproof.txt (test tooling only).

Usage: python3 gen_wycheproof.py WYCHEPROOF_CHECKOUT OUT_TXT
Every test becomes one driver line "X R KHEX UHEX OHEX": R = 1 for tests flagged ZeroSharedSecret
(their shared secret is all zero and x25519_scalarmult must report it), else 0. Wycheproof results
'valid' and 'acceptable' are both compared exactly: acceptable tests here (twist points,
non-canonical u, low-order u) have the RFC 7748 output, which x25519_scalarmult computes. There are
no 'invalid' tests in this file; the converter stops if one appears or a field is not 32 bytes.
"""
import hashlib
import json
import os
import subprocess
import sys

src, out = sys.argv[1], sys.argv[2]
path = os.path.join(src, "testvectors_v1", "x25519_test.json")
raw = open(path, "rb").read()
commit = subprocess.run(["git", "-C", src, "rev-parse", "HEAD"], capture_output=True, text=True,
                        check=True).stdout.strip()
doc = json.loads(raw)
lines = []
counts = {}
for group in doc["testGroups"]:
    if group["curve"] != "curve25519" or group["type"] != "XdhComp":
        sys.exit("unexpected group %s %s" % (group["curve"], group["type"]))
    for t in group["tests"]:
        for f in ("public", "private", "shared"):
            if len(t[f]) != 64:
                sys.exit("tcId %d: %s is not 32 bytes" % (t["tcId"], f))
        if t["result"] not in ("valid", "acceptable"):
            sys.exit("tcId %d: result %s not handled" % (t["tcId"], t["result"]))
        zero = "ZeroSharedSecret" in t["flags"]
        if zero != (t["shared"] == "00" * 32):
            sys.exit("tcId %d: ZeroSharedSecret flag disagrees with the shared secret" % t["tcId"])
        counts[t["result"]] = counts.get(t["result"], 0) + 1
        lines.append("# tcId %d %s %s" % (t["tcId"], t["result"], ",".join(t["flags"])))
        lines.append("X %d %s %s %s" % (1 if zero else 0, t["private"], t["public"], t["shared"]))
if doc["numberOfTests"] != sum(counts.values()):
    sys.exit("numberOfTests %d but %d tests read" % (doc["numberOfTests"], sum(counts.values())))
head = [
    "# Wycheproof x25519_test.json, https://github.com/C2SP/wycheproof/blob/",
    "# %s/testvectors_v1/x25519_test.json" % commit,
    "# sha256 %s, %d tests: %s." % (hashlib.sha256(raw).hexdigest(), doc["numberOfTests"],
                                    ", ".join("%s %d" % kv for kv in sorted(counts.items()))),
    "# Expected rc 1 for the ZeroSharedSecret tests, else 0; every shared secret compared exactly.",
    "# Copyright 2016-2026 The Wycheproof Authors, licensed under the Apache License 2.0",
    "# (wycheproof.LICENSE, upstream LICENSE verbatim). Modified: converted from JSON to this line",
    "# format by gen_wycheproof.py.",
]
with open(out, "w") as f:
    f.write("\n".join(head + lines) + "\n")
