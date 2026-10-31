#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Convert one ACVP-Server sample vector set (gen-val/json-files/<FOLDER>/prompt.json) into records
for acvp.om. Test-vector conversion only: no cryptography happens here. Everything the IUT computes
(hashes, MACs, AES, signature checks, HKDF, the TLS 1.3 key schedule, X25519) is done by the .om
library through acvp.om.

usage: acvp2om.py JSONDIR FOLDER OUTDIR
writes OUTDIR/FOLDER.in.N (record chunks, each under 15 MiB, acvp.om reads at most 16 MiB) and
OUTDIR/FOLDER.map.json: {"records": [[id, tgId, tcId, kind], ...], "na": [[tgId, tcId, reason], ...]}.
Every prompt test is either a record or a not-applicable entry with its reason; nothing is dropped.

Conversions that are pure re-encoding of the prompt (stated in NOTES.md):
- ECDSA: the public key as 04 || X || Y and the signature as r || s, each left-padded to the curve
  width (the library's raw interface; ACVP gives the integers as hex).
- RSA: the public key (n, e) as a DER RSAPublicKey (the library's key format).
- KDA HKDF: IKM = Z || T (SP 800-56C Rev. 2 section 2: the hybrid secret Z' = Z || T) and FixedInfo
  = uPartyInfo || vPartyInfo || L, with partyInfo = partyId || ephemeralData and L the 32-bit
  big-endian output length in bits (the vector set's fixedInfoPattern and concatenation encoding).
"""
import json
import os
import sys

CHUNK = 15 * 1024 * 1024
SHA = {"SHA2-256": 256, "SHA2-384": 384, "SHA2-512": 512}
CURVE = {"P-256": (256, 32), "P-384": (384, 48)}


def h(s):
    return s if s else "-"


def load(jdir, folder, name):
    with open(os.path.join(jdir, folder, name)) as f:
        j = json.load(f)
    return j[1] if isinstance(j, list) else j


class Out:
    def __init__(self):
        self.records, self.na, self.lines = [], [], []

    def rec(self, op, tg, tc, kind, fields):
        rid = len(self.records) + 1
        self.records.append([rid, tg, tc, kind])
        self.lines.append(" ".join([op, str(rid)] + [str(x) for x in fields]) + "\n")

    def skip(self, tg, tests, reason):
        for t in tests:
            self.na.append([tg, t["tcId"], reason])


def der_len(n):
    if n < 128:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(b)]) + b


def der_int(hexs):
    v = bytes.fromhex(hexs).lstrip(b"\0") or b"\0"
    if v[0] & 0x80:
        v = b"\0" + v
    return b"\x02" + der_len(len(v)) + v


def der_rsa(n, e):
    body = der_int(n) + der_int(e)
    return (b"\x30" + der_len(len(body)) + body).hex()


def pad(hexs, width):
    v = bytes.fromhex(hexs).lstrip(b"\0")
    if len(v) > width:
        return None
    return (b"\0" * (width - len(v)) + v).hex()


def sha2(o, p, folder):
    alg = int(folder.split("-")[1])
    for g in p["testGroups"]:
        tg, tt = g["tgId"], g["testType"]
        for t in g["tests"]:
            if tt == "AFT":
                if t["len"] % 8:
                    o.skip(tg, [t], "message length not a whole number of bytes (the library hashes bytes)")
                    continue
                o.rec("H", tg, t["tcId"], "md", [alg, h(t["msg"][: t["len"] // 4])])
            elif tt == "MCT":
                ver = {"standard": 0, "alternate": 1}[g["mctVersion"]]
                o.rec("M", tg, t["tcId"], "mct-md", [alg, ver, h(t["msg"][: t["len"] // 4])])
            elif tt == "LDT":
                m = t["largeMsg"]
                assert m["expansionTechnique"] == "repeating" and m["contentLength"] % 8 == 0
                assert m["fullLength"] % m["contentLength"] == 0
                o.rec("L", tg, t["tcId"], "md", [alg, m["content"], m["fullLength"] // 8])
            else:
                raise SystemExit(f"unknown testType {tt}")


def hmac(o, p, folder):
    alg = SHA[folder.split("-", 1)[1].rsplit("-", 1)[0]]
    for g in p["testGroups"]:
        for t in g["tests"]:
            kl = t.get("keyLen", g.get("keyLen"))
            ml = t.get("msgLen", g.get("msgLen"))
            tl = t.get("macLen", g.get("macLen"))
            if kl % 8 or ml % 8 or tl % 8:
                o.skip(g["tgId"], [t], "bit length not a whole number of bytes")
                continue
            o.rec("T", g["tgId"], t["tcId"], "mac", [alg, h(t["key"]), h(t["msg"]), tl // 8])


def gcm(o, p, folder):
    for g in p["testGroups"]:
        tg = g["tgId"]
        if g.get("ivGen") != "external":
            o.skip(tg, g["tests"], "internal IV generation: the library takes the IV from the caller")
            continue
        assert g["tagLen"] % 8 == 0 and g["ivLen"] % 8 == 0
        for t in g["tests"]:
            if g["direction"] == "encrypt":
                o.rec("G", tg, t["tcId"], "gcm-enc", [t["key"], t["iv"], h(t["aad"]), h(t["pt"]), g["tagLen"] // 8])
            else:
                o.rec("D", tg, t["tcId"], "gcm-dec", [t["key"], t["iv"], h(t["aad"]), h(t["ct"]), t["tag"]])


def ecb(o, p, folder):
    for g in p["testGroups"]:
        tg = g["tgId"]
        if g["direction"] != "encrypt":
            o.skip(tg, g["tests"], "AES decryption (inverse cipher) is not in the library: TLS uses AES only inside GCM")
            continue
        for t in g["tests"]:
            if g["testType"] == "AFT":
                o.rec("B", tg, t["tcId"], "ecb-ct", [t["key"], t["pt"]])
            else:
                o.rec("C", tg, t["tcId"], "ecb-mct", [t["key"], t["pt"]])


def ecdsa(o, p, folder):
    for g in p["testGroups"]:
        tg = g["tgId"]
        if g["curve"] not in CURVE:
            o.skip(tg, g["tests"], f"curve {g['curve']} not in the library (P-256 and P-384 only)")
            continue
        if g["hashAlg"] not in SHA:
            o.skip(tg, g["tests"], f"hash {g['hashAlg']} not in the library (SHA2-256/384/512 only)")
            continue
        if g.get("conformance") == "SP800-106":
            o.skip(tg, g["tests"], "SP 800-106 randomized hashing (message transformed with randomValue before "
                   "hashing): not in the library, which verifies signatures over plain SHA-2 digests as TLS 1.3 does")
            continue
        assert "conformance" not in g, (tg, g["conformance"])
        crv, w = CURVE[g["curve"]]
        for t in g["tests"]:
            qx, qy = pad(t.get("qx", g.get("qx", "")), w), pad(t.get("qy", g.get("qy", "")), w)
            r, s = pad(t["r"], w), pad(t["s"], w)
            assert None not in (qx, qy, r, s), (tg, t["tcId"])
            o.rec("E", tg, t["tcId"], "verify", [crv, SHA[g["hashAlg"]], h(t["message"]), "04" + qx + qy, r + s])


def rsa(o, p, folder):
    for g in p["testGroups"]:
        tg = g["tgId"]
        if g["sigType"] != "pkcs1v1.5":
            why = "ANSI X9.31 signatures are not in the library" if g["sigType"] == "ansx9.31" else \
                f"RSASSA-PSS with {g['hashAlg']}/{g.get('maskFunction', 'mgf1')}: the library's PSS takes SHA2-256/384/512 with MGF1 only"
            if g["sigType"] == "pss" and g["hashAlg"] in SHA and g.get("maskFunction", "mgf1") == "mgf1":
                raise SystemExit("in-scope PSS group: add a PSS record type")
            o.skip(tg, g["tests"], why)
            continue
        if g["hashAlg"] not in SHA:
            o.skip(tg, g["tests"], f"hash {g['hashAlg']} not in the library (SHA2-256/384/512 only)")
            continue
        key = der_rsa(g["n"], g["e"])
        for t in g["tests"]:
            o.rec("R", tg, t["tcId"], "verify", [SHA[g["hashAlg"]], key, h(t["message"]), t["signature"]])


def party(pi):
    return pi["partyId"] + pi.get("ephemeralData", "")


def kda(o, p, folder):
    for g in p["testGroups"]:
        tg, val = g["tgId"], g["testType"] == "VAL"
        multi = g.get("multiExpansion", False)
        cfg = g["kdfMultiExpansionConfiguration"] if multi else g["kdfConfiguration"]
        assert cfg["kdfType"] == "hkdf"
        if cfg["hmacAlg"] not in SHA:
            o.skip(tg, g["tests"], f"HMAC {cfg['hmacAlg']} not in the library (SHA2-256/384/512 only)")
            continue
        alg = SHA[cfg["hmacAlg"]]
        if not multi:
            assert cfg["fixedInfoPattern"] == "uPartyInfo||vPartyInfo||l" and cfg["fixedInfoEncoding"] == "concatenation"
        for t in g["tests"]:
            if multi:
                kp = t["kdfMultiExpansionParameter"]
                assert kp["hmacAlg"] == cfg["hmacAlg"]
                its = [(i["fixedInfo"], i["l"]) for i in kp["iterationParameters"]]
            else:
                kp = t["kdfParameter"]
                fi = party(t["fixedInfoPartyU"]) + party(t["fixedInfoPartyV"]) + kp["l"].to_bytes(4, "big").hex()
                its = [(fi, kp["l"])]
            assert all(l % 8 == 0 for _, l in its)
            ikm = kp["z"] + kp.get("t", "")
            fields = [alg, h(kp["salt"]), h(ikm), len(its)]
            if val:
                dkms = t["dkms"] if multi else [t["dkm"]]
                for (fi, l), d in zip(its, dkms):
                    fields += [h(fi), l // 8, h(d)]
                o.rec("V", tg, t["tcId"], "kda-val", fields)
            else:
                for fi, l in its:
                    fields += [h(fi), l // 8]
                o.rec("K", tg, t["tcId"], "kda-multi" if multi else "kda", fields)


def tls13(o, p, folder):
    for g in p["testGroups"]:
        tg, mode = g["tgId"], g["runningMode"]
        alg = SHA[g["hmacAlg"]]
        for t in g["tests"]:
            psk = t.get("psk", "") if "PSK" in mode else ""
            dhe = t.get("dhe", "") if "DHE" in mode else ""
            msgs = [t["helloClientRandom"], t["helloServerRandom"], t["finishedServerRandom"], t["finishedClientRandom"]]
            o.rec("S", tg, t["tcId"], "tls13", [alg, h(psk), h(dhe)] + [h(m) for m in msgs])
            if alg == 256 and len(psk) == 64 and len(dhe) == 64:
                o.rec("N", tg, t["tcId"], "tls13-native", [psk, dhe] + [h(m) for m in msgs[:3]])


def xecdh(o, p, folder, ip):
    priv = {(g["tgId"], t["tcId"]): t["privateIut"] for g in ip["testGroups"] for t in g["tests"]}
    for g in p["testGroups"]:
        tg = g["tgId"]
        if g["curve"] != "Curve25519":
            o.skip(tg, g["tests"], f"{g['curve']} not in the library (X25519 only)")
            continue
        for t in g["tests"]:
            o.rec("X", tg, t["tcId"], "xecdh", [priv[(tg, t["tcId"])], t["publicServer"]])


def kasecc(o, p, folder):
    for g in p["testGroups"]:
        crv = g["domainParameterGenerationMode"]
        if crv in ("P-256", "P-384"):
            raise SystemExit(f"KAS-ECC group {g['tgId']} on {crv}: wire it to ecdh.om before running")
        o.skip(g["tgId"], g["tests"], f"KAS-ECC {g['scheme']} on {crv}: the library implements SP 800-56A "
               "ECC CDH only on P-256/P-384 (ecdh.om, approved mode); this sample group uses another curve")


def mlkem(o, p, folder):
    for g in p["testGroups"]:
        tg = g["tgId"]
        if g["parameterSet"] != "ML-KEM-768":
            o.skip(tg, g["tests"], f"{g['parameterSet']} not in the library (ML-KEM-768 only)")
            continue
        fn = g.get("function", "keyGen")
        for t in g["tests"]:
            if fn == "keyGen":
                o.rec("Q", tg, t["tcId"], "mlkem-keygen", [t["d"], t["z"]])
            elif fn == "encapsulation":
                o.rec("P", tg, t["tcId"], "mlkem-encap", [t["ek"], t["m"]])
            elif fn == "decapsulation":
                o.rec("U", tg, t["tcId"], "mlkem-decap", [t["dk"], t["c"]])
            elif fn == "decapsulationKeyCheck":
                o.rec("Y", tg, t["tcId"], "mlkem-check", [t["dk"]])
            elif fn == "encapsulationKeyCheck":
                o.rec("Z", tg, t["tcId"], "mlkem-check", [t["ek"]])
            else:
                raise SystemExit(f"unknown ML-KEM function {fn}")


HANDLERS = [("SHA2-", sha2), ("HMAC-SHA2-", hmac), ("ACVP-AES-GCM", gcm), ("ACVP-AES-ECB", ecb),
            ("ECDSA-SigVer", ecdsa), ("RSA-SigVer", rsa), ("KDA-HKDF", kda), ("TLS-v1.3-KDF", tls13),
            ("KAS-ECC-SSC", kasecc), ("ML-KEM-", mlkem)]


def main():
    jdir, folder, outdir = sys.argv[1:4]
    p = load(jdir, folder, "prompt.json")
    o = Out()
    if folder.startswith("XECDH"):
        xecdh(o, p, folder, load(jdir, folder, "internalProjection.json"))
    else:
        fn = [f for pre, f in HANDLERS if folder.startswith(pre)]
        if not fn:
            raise SystemExit(f"no converter for {folder}")
        fn[0](o, p, folder)
    ntests = sum(len(g["tests"]) for g in p["testGroups"])
    covered = {(r[1], r[2]) for r in o.records} | {(n[0], n[1]) for n in o.na}
    assert len(covered) == ntests, (folder, len(covered), ntests)
    os.makedirs(outdir, exist_ok=True)
    for f in os.listdir(outdir):
        if f.startswith(folder + ".in."):
            os.remove(os.path.join(outdir, f))
    chunk, size, k = [], 0, 0
    for line in o.lines + [None]:
        if line is None or (chunk and size + len(line) > CHUNK):
            if chunk:
                with open(os.path.join(outdir, f"{folder}.in.{k}"), "w") as f:
                    f.writelines(chunk)
                k += 1
            chunk, size = [], 0
        if line is not None:
            chunk.append(line)
            size += len(line)
    with open(os.path.join(outdir, f"{folder}.map.json"), "w") as f:
        json.dump({"folder": folder, "prompt_tests": ntests, "records": o.records, "na": o.na}, f)
    print(f"{folder} tests={ntests} records={len(o.records)} not_applicable={len(o.na)} chunks={k}")


if __name__ == "__main__":
    main()
