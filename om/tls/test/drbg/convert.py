#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Vector converter (test tooling only; the DRBG itself is om/tls/drbg.om). Writes the HMAC_DRBG
# SHA-256 records that test/drbg/driver.om reads:
#   python3 convert.py ACVP_DIR CAVP_DIR OUTDIR
# ACVP_DIR holds hmacDRBG-1.0/ and hmacDRBG-SP800-90Ar1/ internalProjection.json from
# usnistgov/ACVP-Server gen-val/json-files at 975de31eb83d87039ec88934fdc47d8c312b892d.
# CAVP_DIR holds drbgvectors_{no_reseed,pr_false,pr_true}/HMAC_DRBG.rsp from the CAVP
# drbgtestvectors.zip (sha256 5f7e5658ebd5b4e6785a7b12fa32333511d2acc2f2d9c5ae1ffa16b699377769).
# Only the SHA-256 groups / [SHA-256] sections are converted (the module implements SHA-256 only).
# Record format (driver.om): D NB EI NONCE PS OP .. OP = RB, "-" for an empty field;
# OP is "G AI", "R AI EI" (reseed) or "P AI EI" (prediction-resistance generate).
import hashlib, json, os, sys


def h(x):
    return x.lower() if x else "-"


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def acvp(path, rev):
    out = []
    j = json.load(open(path))
    for g in j["testGroups"]:
        if g["mode"] != "SHA2-256" or g["derFunc"]:
            continue
        nb = g["returnedBitsLen"] // 8
        for t in g["tests"]:
            ops = []
            for o in t["otherInput"]:
                if o["intendedUse"] == "reSeed":
                    ops.append("R %s %s" % (h(o["additionalInput"]), h(o["entropyInput"])))
                elif g["predResistance"]:
                    ops.append("P %s %s" % (h(o["additionalInput"]), h(o["entropyInput"])))
                else:
                    assert not o["entropyInput"]
                    ops.append("G %s" % h(o["additionalInput"]))
            assert len(t["returnedBits"]) == 2 * nb
            out.append("D %d %s %s %s %s = %s" % (nb, h(t["entropyInput"]), h(t["nonce"]),
                       h(t["persoString"]), " ".join(ops), h(t["returnedBits"])))
        out_tag = "# acvp %s tgId %d predResistance %s returnedBitsLen %d tests %d" % (
            rev, g["tgId"], g["predResistance"], g["returnedBitsLen"], len(g["tests"]))
        out.insert(len(out) - len(g["tests"]), out_tag)
    return out


def cavp(path, kind):
    out = []
    sec = None
    nb = 0
    rec = []
    for line in open(path):
        line = line.strip()
        if line.startswith("[") and "=" not in line:
            sec = line
            continue
        if sec != "[SHA-256]":
            continue
        if line.startswith("[ReturnedBitsLen"):
            nb = int(line.split("=")[1].strip(" ]")) // 8
            continue
        if line.startswith("[") or not line or line.startswith("#"):
            continue
        k, _, v = line.partition("=")
        k = k.strip()
        v = v.strip()
        if k == "COUNT":
            rec = []
            continue
        rec.append((k, v))
        if k != "ReturnedBits":
            continue
        d = dict()
        seq = []
        for kk, vv in rec:
            seq.append((kk, vv))
            d.setdefault(kk, vv)
        names = [kk for kk, _ in seq]
        vals = [vv for _, vv in seq]
        ei, no, ps = vals[0], vals[1], vals[2]
        assert names[:3] == ["EntropyInput", "Nonce", "PersonalizationString"], names
        if kind == "no_reseed":
            assert names[3:] == ["AdditionalInput", "AdditionalInput", "ReturnedBits"], names
            ops = ["G %s" % h(vals[3]), "G %s" % h(vals[4])]
        elif kind == "pr_false":
            assert names[3:] == ["EntropyInputReseed", "AdditionalInputReseed", "AdditionalInput",
                                 "AdditionalInput", "ReturnedBits"], names
            ops = ["R %s %s" % (h(vals[4]), h(vals[3])), "G %s" % h(vals[5]), "G %s" % h(vals[6])]
        else:
            assert names[3:] == ["AdditionalInput", "EntropyInputPR", "AdditionalInput",
                                 "EntropyInputPR", "ReturnedBits"], names
            ops = ["P %s %s" % (h(vals[3]), h(vals[4])), "P %s %s" % (h(vals[5]), h(vals[6]))]
        assert len(vals[-1]) == 2 * nb
        out.append("D %d %s %s %s %s = %s" % (nb, h(ei), h(no), h(ps), " ".join(ops), h(vals[-1])))
        rec = []
    return out


def main():
    a, c, o = sys.argv[1:4]
    lines = ["# HMAC_DRBG SHA-256 vectors for om/tls/test/drbg/driver.om, written by convert.py",
             "# ACVP-Server 975de31eb83d87039ec88934fdc47d8c312b892d gen-val/json-files"]
    body = []
    for rev, d in (("1.0", "hmacDRBG-1.0"), ("SP800-90Ar1", "hmacDRBG-SP800-90Ar1")):
        p = os.path.join(a, d, "internalProjection.json")
        lines.append("# %s/internalProjection.json sha256 %s" % (d, sha(p)))
        body += acvp(p, rev)
    open(os.path.join(o, "acvp.txt"), "w").write("\n".join(lines + body) + "\n")
    lines = ["# HMAC_DRBG SHA-256 vectors for om/tls/test/drbg/driver.om, written by convert.py",
             "# CAVP drbgtestvectors.zip sha256 5f7e5658ebd5b4e6785a7b12fa32333511d2acc2f2d9c5ae1ffa16b699377769"]
    body = []
    for kind in ("no_reseed", "pr_false", "pr_true"):
        p = os.path.join(c, "drbgvectors_" + kind, "HMAC_DRBG.rsp")
        lines.append("# drbgvectors_%s/HMAC_DRBG.rsp sha256 %s" % (kind, sha(p)))
        r = cavp(p, kind)
        body.append("# cavp %s [SHA-256] records %d" % (kind, len(r)))
        body += r
    open(os.path.join(o, "cavp.txt"), "w").write("\n".join(lines + body) + "\n")


main()
