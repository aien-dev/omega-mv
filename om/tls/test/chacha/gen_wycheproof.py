#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# gen_wycheproof.py: test tooling only. Converts Wycheproof testvectors_v1 JSON into driver records.
#   aead    chacha20_poly1305_test.json -> A records. Expected code: "valid" 0; "invalid" with a
#           96-bit nonce 1 (tag refused); "invalid" with any other nonce size 2 (refused by length).
#   counter chacha20_counter_test.json, ChaCha20-IETF groups only -> C records: "valid" 0,
#           "invalid" (counter would wrap past 2^32 - 1) 2. XChaCha20 groups are not implemented
#           here and are counted as NOT_RUN on stderr.
# The file has no "acceptable" results; one would abort the conversion.
# Usage: python3 gen_wycheproof.py aead|counter FILE > OUT
import json
import sys

mode, path = sys.argv[1], sys.argv[2]
d = json.load(open(path))
out = []
counts = {}
notrun = 0
for g in d["testGroups"]:
    if mode == "counter" and g["algorithm"] != "ChaCha20-IETF":
        notrun += len(g["tests"])
        continue
    for t in g["tests"]:
        res = t["result"]
        if res not in ("valid", "invalid"):
            sys.exit("unexpected result %r in tcId %d" % (res, t["tcId"]))
        if mode == "aead":
            if res == "valid":
                code = 0
            elif g["ivSize"] == 96 and len(t["iv"]) == 24:
                code = 1
            else:
                code = 2
            out.append("# tcId %d %s %s ivSize %d" % (t["tcId"], res, t["comment"], g["ivSize"]))
            out.append("A %s %s %s %s %d" % (t["key"] + t["iv"], t["aad"] or "-", t["msg"] or "-",
                                             (t["ct"] + t["tag"]) or "-", code))
        else:
            code = 0 if res == "valid" else 2
            ctr = int.from_bytes(bytes.fromhex(t["initialCounter"]), "little")
            out.append("# tcId %d %s %s" % (t["tcId"], res, t["comment"]))
            out.append("C %s %d %s %s %d" % (t["key"] + t["iv"], ctr, t["msg"] or "-",
                                             t.get("ct") or t["msg"] or "-", code))
        counts[code] = counts.get(code, 0) + 1
print("# Converted by gen_wycheproof.py %s from Wycheproof %s" % (mode, path.split("/")[-1]))
print("# expected codes %s; NOT_RUN %d" % (" ".join("%d:%d" % kv for kv in sorted(counts.items())), notrun))
print("\n".join(out))
sys.stderr.write("%s: records %d codes %s NOT_RUN %d\n" % (path.split("/")[-1], sum(counts.values()),
                 sorted(counts.items()), notrun))
