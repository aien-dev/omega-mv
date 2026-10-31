# SPDX-License-Identifier: Apache-2.0
"""Convert C2SP x509-limbo (limbo.json) into driver.om R records (test generator, not shipped).

usage: python3 gen_limbo.py LIMBO_JSON OUTDIR
Writes OUTDIR/cases.txt (R records), OUTDIR/meta.tsv (name, id, expected, host kind, flags) and, per
case, OUTDIR/c/NAME.store.pem plus OUTDIR/c/NAME.N.der (leaf first, then the untrusted
intermediates in limbo order). PEM bodies are base64-decoded directly so malformed certificates
reach the validator unchanged. Cases the driver cannot express are written to meta.tsv with a
reason instead of an R record:
  CLIENT   validation_kind CLIENT (om/tls validates server chains only)
  NOHOST   no usable peer name and no DNS SAN in the leaf to stand in for one
The validator always checks a DNS host name, so for a case with no expected_peer_name the
first DNS SAN of the leaf (a wildcard's '*' replaced by 'x') stands in (flag SANHOST).
"""
import base64
import datetime
import json
import os
import re
import sys

PEM = re.compile(rb"-----BEGIN ([A-Z0-9 ]+)-----(.*?)-----END \1-----", re.S)


def ders(text):
    out = []
    for kind, body in PEM.findall(text.encode()):
        if kind != b"CERTIFICATE":
            continue
        out.append(base64.b64decode(b"".join(body.split()), validate=False))
    return out


def san_host(der):
    try:
        from cryptography import x509
        cert = x509.load_der_x509_certificate(der)
        san = cert.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
        for name in san.get_values_for_type(x509.DNSName):
            if name and " " not in name:
                return name.replace("*", "x", 1)
    except Exception:
        return None
    return None


def main():
    src, out = sys.argv[1], sys.argv[2]
    cases = json.load(open(src))["testcases"]
    os.makedirs(os.path.join(out, "c"), exist_ok=True)
    now = int(datetime.datetime.now(datetime.timezone.utc).timestamp())
    rec = open(os.path.join(out, "cases.txt"), "w")
    meta = open(os.path.join(out, "meta.tsv"), "w")
    for i, t in enumerate(cases):
        name = "L%05d" % i
        flags = []
        if t["validation_kind"] != "SERVER":
            meta.write("%s\t%s\t%s\t-\tSKIP:CLIENT\n" % (name, t["id"], t["expected_result"]))
            continue
        leaf = ders(t["peer_certificate"])
        inter = [d for p in t["untrusted_intermediates"] for d in ders(p)]
        peer = t["expected_peer_name"]
        if peer is None:
            host = san_host(leaf[0]) if leaf else None
            kind = "SAN"
            flags.append("SANHOST")
        else:
            host = peer["value"]
            kind = peer["kind"]
        if not host or re.search(r"\s", host) or len(host) > 290:
            meta.write("%s\t%s\t%s\t%s\tSKIP:NOHOST\n" % (name, t["id"], t["expected_result"], kind))
            continue
        if t["validation_time"]:
            ts = int(datetime.datetime.fromisoformat(t["validation_time"]).timestamp())
        else:
            ts = now
            flags.append("NOW")
        if t.get("crls"):
            flags.append("CRL")
        if t["max_chain_depth"] is not None:
            flags.append("DEPTH%d" % t["max_chain_depth"])
        if t["extended_key_usage"]:
            flags.append("EKU:" + "+".join(t["extended_key_usage"]))
        files = []
        for j, der in enumerate(leaf[:1] + inter):
            f = "c/%s.%d.der" % (name, j)
            open(os.path.join(out, f), "wb").write(der)
            files.append(f)
        store = "c/%s.store.pem" % name
        open(os.path.join(out, store), "w").write("".join(t["trusted_certs"]))
        if not files:
            files = ["c/%s.missing.der" % name]
        rec.write("R %s %d %s %s %s\n" % (name, max(ts, 0), host, store, " ".join(files)))
        meta.write("%s\t%s\t%s\t%s\t%s\n" % (name, t["id"], t["expected_result"], kind,
                                             ",".join(flags + t["features"]) or "-"))
    rec.close()
    meta.close()


if __name__ == "__main__":
    main()
