#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Build an ACVP response JSON from acvp.om's output. Formatting only: values are copied from the
driver's output; booleans come from the library's return codes (signature verify rc 0, GCM open rc
0/1, KDA VAL flag decided in .om).

usage: om2acvp.py JSONDIR FOLDER WORKDIR
reads WORKDIR/FOLDER.map.json and WORKDIR/FOLDER.out.* (the driver's stdout for each .in.N chunk),
writes WORKDIR/FOLDER.response.json in the expectedResults.json shape (vsId, algorithm, revision,
testGroups[tgId, tests[tcId, result fields]]) and, for TLS-v1.3-KDF, also
WORKDIR/FOLDER.native.response.json (the four secrets from tls.om's own tls_xtr/tls_drv path).
A record whose output is missing, malformed or carries an unexpected return code gets
{"error": ...} in place of result fields, which the comparator counts as a failure.
"""
import glob
import json
import os
import sys

TLS = ["clientEarlyTrafficSecret", "earlyExporterMasterSecret", "clientHandshakeTrafficSecret",
       "serverHandshakeTrafficSecret", "clientApplicationTrafficSecret", "serverApplicationTrafficSecret",
       "exporterMasterSecret", "resumptionMasterSecret"]
NATIVE = TLS[2:6]


def hx(s):
    return "" if s == "-" else s


def result(kind, f):
    """f: output fields after the ID. Returns the ACVP result dict or raises ValueError."""
    if f == ["BAD"] or not f:
        raise ValueError("driver rejected the record")
    if kind == "md":
        return {"md": hx(f[0])}
    if kind == "mct-md":
        if len(f) != 100:
            raise ValueError(f"{len(f)} MCT digests")
        return {"resultsArray": [{"md": hx(x)} for x in f]}
    if kind == "mac":
        return {"mac": hx(f[0])}
    if kind == "gcm-enc":
        if f[0] != "0":
            raise ValueError(f"aes_gcm_seal rc {f[0]}")
        return {"ct": hx(f[1]), "tag": hx(f[2])}
    if kind == "gcm-dec":
        if f[0] == "0":
            return {"pt": hx(f[1])}
        if f[0] == "1":
            return {"testPassed": False}
        raise ValueError(f"aes_gcm_open rc {f[0]}")
    if kind == "ecb-ct":
        return {"ct": hx(f[0])}
    if kind == "ecb-mct":
        if len(f) != 300:
            raise ValueError(f"{len(f)} MCT fields")
        return {"resultsArray": [{"key": f[3 * i], "pt": f[3 * i + 1], "ct": f[3 * i + 2]} for i in range(100)]}
    if kind == "verify":
        return {"testPassed": f[0] == "0"}
    if kind in ("kda", "kda-multi"):
        if f[-1] != "1":
            raise ValueError("hkdf_expand returned non-zero (ok flag 0)")
        return {"dkm": hx(f[0])} if kind == "kda" else {"dkms": [hx(x) for x in f[:-1]]}
    if kind == "kda-val":
        return {"testPassed": f[0] == "1"}
    if kind == "tls13":
        if len(f) != 8:
            raise ValueError("expected 8 secrets")
        return dict(zip(TLS, f))
    if kind == "tls13-native":
        if f[0] != "0" or len(f) != 5:
            raise ValueError(f"tls.om rc {f[0]}")
        return dict(zip(NATIVE, f[1:]))
    if kind == "xecdh":
        if f[0] != "0" or f[2] != "0":
            raise ValueError(f"x25519 rc {f[0]} {f[2]}")
        return {"publicIut": f[1], "z": f[3]}
    if kind == "mlkem-keygen":
        if f[0] != "0":
            raise ValueError(f"mlkem_keygen rc {f[0]}")
        return {"ek": f[1], "dk": f[2]}
    if kind == "mlkem-encap":
        if f[0] != "0":
            raise ValueError(f"mlkem_encaps rc {f[0]}")
        return {"c": f[1], "k": f[2]}
    if kind == "mlkem-decap":
        if f[0] != "0":
            raise ValueError(f"mlkem_decaps rc {f[0]}")
        return {"k": f[1]}
    if kind == "mlkem-check":
        return {"testPassed": f[0] == "0"}
    raise ValueError(f"unknown kind {kind}")


def main():
    jdir, folder, work = sys.argv[1:4]
    with open(os.path.join(jdir, folder, "prompt.json")) as fh:
        pj = json.load(fh)
    head = pj[1] if isinstance(pj, list) else pj
    m = json.load(open(os.path.join(work, folder + ".map.json")))
    out = {}
    for path in sorted(glob.glob(os.path.join(work, folder + ".out.*"))):
        for line in open(path):
            f = line.split()
            if f and f[0].isdigit():
                out.setdefault(int(f[0]), f[1:])
    resp = {"main": {}, "native": {}}
    for rid, tg, tc, kind in m["records"]:
        try:
            r = result(kind, out.get(rid, []))
        except (ValueError, IndexError) as e:
            r = {"error": f"record {rid}: {e}"}
        resp["native" if kind == "tls13-native" else "main"].setdefault(tg, []).append(dict(tcId=tc, **r))
    for name, groups in resp.items():
        if not groups:
            continue
        doc = [{"acvVersion": "1.0"}, {"vsId": head["vsId"], "algorithm": head["algorithm"],
               "mode": head.get("mode"), "revision": head["revision"], "isSample": head.get("isSample"),
               "testGroups": [{"tgId": tg, "tests": ts} for tg, ts in sorted(groups.items())]}]
        if doc[1]["mode"] is None:
            del doc[1]["mode"]
        sfx = ".response.json" if name == "main" else ".native.response.json"
        with open(os.path.join(work, folder + sfx), "w") as fh:
            json.dump(doc, fh, indent=1)


if __name__ == "__main__":
    main()
