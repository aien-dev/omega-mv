# SPDX-License-Identifier: Apache-2.0
# Test tooling only (no primitive here): converts Wycheproof testvectors_v1/aes_gcm_test.json into
# driver records "W TCID V|I KEY IV AAD MSG CT TAG" (hex, "-" for empty), with a provenance header.
# Usage: python3 gen_wycheproof.py WYCHEPROOF_DIR > vectors/wycheproof_aes_gcm.txt
import hashlib
import json
import os
import subprocess
import sys

root = sys.argv[1]
rel = "testvectors_v1/aes_gcm_test.json"
path = os.path.join(root, rel)
raw = open(path, "rb").read()
doc = json.loads(raw)
commit = subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True,
                        text=True, check=True).stdout.strip()


def h(s):
    return s.lower() if s else "-"


out = sys.stdout
out.write("# Wycheproof %s (%s, %d tests)\n" % (rel, doc["algorithm"], doc["numberOfTests"]))
out.write("# Source: https://github.com/C2SP/wycheproof commit %s\n" % commit)
out.write("# sha256 %s\n" % hashlib.sha256(raw).hexdigest())
out.write("# License: Apache-2.0 (see wycheproof-LICENSE beside this file)\n")
out.write("# Format: W TCID V|I KEY IV AAD MSG CT TAG (result valid -> V, invalid -> I)\n")
n = 0
for g in doc["testGroups"]:
    for t in g["tests"]:
        r = t["result"]
        if r not in ("valid", "invalid"):
            sys.exit("unexpected result %r in tcId %d" % (r, t["tcId"]))
        out.write("W %d %s %s %s %s %s %s %s\n" % (
            t["tcId"], "V" if r == "valid" else "I", h(t["key"]), h(t["iv"]), h(t["aad"]),
            h(t["msg"]), h(t["ct"]), h(t["tag"])))
        n += 1
if n != doc["numberOfTests"]:
    sys.exit("count %d != numberOfTests %d" % (n, doc["numberOfTests"]))
