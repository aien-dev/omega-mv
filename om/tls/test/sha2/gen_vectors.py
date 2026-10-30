#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only (no primitive is implemented here): converts the upstream vector sources into the
# flat record files driver.om reads, and computes the generated long-message digests with hashlib as
# an independent oracle. Usage, from the omega-mv root:
#   python3 om/tls/test/sha2/gen_vectors.py /tmp/wycheproof
# Inputs: vectors/rfc4231.txt, vectors/rfc5869.txt (RFC texts, copied verbatim from rfc-editor.org),
# WYCHEPROOF/testvectors_v1/{hmac,hkdf}_sha{256,384,512}_test.json. NIST SHAVS .rsp files are
# converted by run.sh itself.
import hashlib
import hmac
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VEC = os.path.join(HERE, "vectors")


def sha256_file(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()


def hx(s):
    return s if s else "-"


def rfc_fields(text, start, stop):
    """Hex fields of one test-case section: name -> hex string (continuation lines joined)."""
    s = text.rindex(start)  # the last occurrence: the first is the table of contents
    sec = text[s:text.index(stop, s)] if stop else text[s:]
    out, cur = {}, None
    for line in sec.splitlines():
        m = re.match(r"^   (\w[\w-]*)\s*=?\s+(?:0x)?([0-9a-f]+)\b", line)
        if m and (m.group(1) in ("Key", "Data", "IKM", "salt", "info", "PRK", "OKM")
                  or m.group(1).startswith("HMAC-SHA-")):
            cur = m.group(1)
            out[cur] = m.group(2)
            continue
        m = re.match(r"^ {10,}([0-9a-f]+)\b", line)
        if m and cur:
            out[cur] += m.group(1)
            continue
        if line.strip() == "" or line.startswith("RFC ") or line.startswith("Nystrom") \
                or line.startswith("Krawczyk") or "\f" in line:
            continue
        cur = None
    return out


def gen_rfc4231():
    src = os.path.join(VEC, "rfc4231.txt")
    text = open(src).read()
    heads = ["4.2.  Test Case 1", "4.3.  Test Case 2", "4.4.  Test Case 3", "4.5.  Test Case 4",
             "4.6.  Test Case 5", "4.7.  Test Case 6", "4.8.  Test Case 7", "5.  Security"]
    lines = ["# RFC 4231 HMAC-SHA-256/384/512 test cases 1-7 (case 5 truncated to 128 bits).",
             "# source https://www.rfc-editor.org/rfc/rfc4231.txt sha256 " + sha256_file(src),
             "# record: T alg KEY MSG TAG 1"]
    n = 0
    for i in range(7):
        f = rfc_fields(text, heads[i], heads[i + 1])
        key, data = bytes.fromhex(f["Key"]), bytes.fromhex(f["Data"])
        for alg in (256, 384, 512):
            tag = f["HMAC-SHA-%d" % alg]
            full = hmac.new(key, data, "sha%d" % alg).hexdigest()
            assert full.startswith(tag), (i + 1, alg)  # parser check against hashlib
            assert (i == 4) == (len(tag) < len(full)), (i + 1, alg)
            lines.append("T %d %s %s %s 1" % (alg, f["Key"], f["Data"], tag))
            n += 1
    open(os.path.join(VEC, "rfc4231-hmac.txt"), "w").write("\n".join(lines) + "\n")
    return n


def gen_rfc5869():
    src = os.path.join(VEC, "rfc5869.txt")
    text = open(src).read()
    heads = ["A.1.  Test Case 1", "A.2.  Test Case 2", "A.3.  Test Case 3", "A.4.  Test Case 4"]
    lines = ["# RFC 5869 HKDF-SHA-256 test cases 1-3 (A.1-A.3; A.4-A.7 are SHA-1, not implemented).",
             "# source https://www.rfc-editor.org/rfc/rfc5869.txt sha256 " + sha256_file(src),
             "# records: P alg SALT IKM PRK, X alg SALT IKM INFO L OKM 1"]
    n = 0
    for i in range(3):
        f = rfc_fields(text, heads[i], heads[i + 1])
        salt, info = f.get("salt", ""), f.get("info", "")
        ikm, prk, okm = f["IKM"], f["PRK"], f["OKM"]
        k = hmac.new(bytes.fromhex(salt) if salt else b"\0" * 32, bytes.fromhex(ikm), "sha256").hexdigest()
        assert k == prk, i + 1
        lines.append("P 256 %s %s %s" % (hx(salt), ikm, prk))
        lines.append("X 256 %s %s %s %d %s 1" % (hx(salt), ikm, hx(info), len(okm) // 2, okm))
        n += 2
    open(os.path.join(VEC, "rfc5869-hkdf.txt"), "w").write("\n".join(lines) + "\n")
    return n


def wy_header(wy, names):
    rev = subprocess.run(["git", "-C", wy, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    out = ["# Wycheproof (C2SP/wycheproof, Apache-2.0, LICENSE beside this file) commit " + rev]
    for nm in names:
        out.append("# %s sha256 %s" % (nm, sha256_file(os.path.join(wy, "testvectors_v1", nm))))
    return out


def gen_wy_hmac(wy):
    names = ["hmac_sha%d_test.json" % a for a in (256, 384, 512)]
    lines = wy_header(wy, names) + ["# record: T alg KEY MSG TAG V (V 1 valid, 0 invalid; TAG = tagSize/8 bytes)"]
    cnt = {}
    for alg, nm in zip((256, 384, 512), names):
        d = json.load(open(os.path.join(wy, "testvectors_v1", nm)))
        for g in d["testGroups"]:
            for t in g["tests"]:
                assert len(t["tag"]) * 4 == g["tagSize"]
                r = t["result"]
                assert r in ("valid", "invalid"), r
                cnt[(alg, r)] = cnt.get((alg, r), 0) + 1
                lines.append("T %d %s %s %s %d" % (alg, hx(t["key"]), hx(t["msg"]), t["tag"], r == "valid"))
    open(os.path.join(VEC, "wycheproof-hmac.txt"), "w").write("\n".join(lines) + "\n")
    return cnt


def gen_wy_hkdf(wy):
    names = ["hkdf_sha%d_test.json" % a for a in (256, 384, 512)]
    lines = wy_header(wy, names) + ["# record: X alg SALT IKM INFO L OKM V (V 0 = expand must refuse)"]
    cnt = {}
    for alg, nm in zip((256, 384, 512), names):
        d = json.load(open(os.path.join(wy, "testvectors_v1", nm)))
        for g in d["testGroups"]:
            for t in g["tests"]:
                r = t["result"]
                assert r in ("valid", "invalid"), r
                if r == "invalid":
                    assert t["size"] > 255 * alg // 8, t["tcId"]
                cnt[(alg, r)] = cnt.get((alg, r), 0) + 1
                lines.append("X %d %s %s %s %d %s %d" % (alg, hx(t["salt"]), hx(t["ikm"]), hx(t["info"]),
                                                       t["size"], hx(t["okm"]), r == "valid"))
    open(os.path.join(VEC, "wycheproof-hkdf.txt"), "w").write("\n".join(lines) + "\n")
    return cnt


def gen_long():
    lines = ["# Generated messages: byte i = (7 i + 3) mod 256, streamed by driver.om in random chunks.",
             "# Expected digests from python3 hashlib " + sys.version.split()[0] + " (independent oracle).",
             "# record: G alg N MD"]
    sizes = [0, 1, 55, 56, 111, 112, 127, 128, 1000003, 16777216, 16777259]
    n = 0
    for size in sizes:
        msg = bytes((7 * i + 3) & 255 for i in range(size))
        for alg in (256, 384, 512):
            lines.append("G %d %d %s" % (alg, size, hashlib.new("sha%d" % alg, msg).hexdigest()))
            n += 1
    open(os.path.join(VEC, "generated-long.txt"), "w").write("\n".join(lines) + "\n")
    return n


if __name__ == "__main__":
    wy = sys.argv[1] if len(sys.argv) > 1 else "/tmp/wycheproof"
    print("rfc4231", gen_rfc4231())
    print("rfc5869", gen_rfc5869())
    print("wycheproof-hmac", gen_wy_hmac(wy))
    print("wycheproof-hkdf", gen_wy_hkdf(wy))
    print("generated-long", gen_long())
