# SPDX-License-Identifier: Apache-2.0
"""Judge driver.om R results against x509-limbo expectations (test oracle, not shipped).

usage: python3 limbo.py META_TSV RESULTS_TXT ALLOW_TXT
A case is
  agree      limbo SUCCESS and X509_OK, or limbo FAILURE and any reject code
  ACCEPT     limbo FAILURE but X509_OK: a chain limbo says must be refused was accepted
  reject     limbo SUCCESS but a reject code: stricter than limbo
ALLOW_TXT lists the explained differences, one per line: "accept|reject ID-GLOB CODE|* reason";
the first matching line explains a case. Exit 1 on any unexplained ACCEPT or reject, on a case with
no result, on a trap or a STORE/LIST setup failure, and on an allow line that matches nothing.
Prints per-category counts and the final line
"LIMBO PASS|FAIL cases=N agree=A explained_accept=X explained_reject=R skipped=S unexplained=U".
"""
import collections
import fnmatch
import sys


def main():
    meta, res, allow = sys.argv[1:4]
    rules = []
    for line in open(allow):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        kind, glob, code, why = line.split(None, 3)
        rules.append([kind, glob, code, why, 0])
    got = {}
    for line in open(res):
        f = line.split()
        if len(f) == 3 and f[0] == "RESULT":
            got[f[1]] = f[2]
    count = collections.Counter()
    bad = []
    n = 0
    for line in open(meta):
        name, cid, want, kind, flags = line.rstrip("\n").split("\t")
        if flags.startswith("SKIP:"):
            count["skipped " + flags[5:]] += 1
            continue
        n += 1
        g = got.get(name)
        if g is None or g in ("STORE", "LIST"):
            bad.append("%s %s no result (%s)" % (name, cid, g))
            continue
        ok = g == "X509_OK"
        if (want == "SUCCESS") == ok:
            count["agree"] += 1
            continue
        k = "accept" if ok else "reject"
        for r in rules:
            if r[0] == k and fnmatch.fnmatchcase(cid, r[1]) and r[2] in ("*", g):
                r[4] += 1
                count["explained %s: %s (%s)" % (k, r[3], g)] += 1
                break
        else:
            bad.append("UNEXPLAINED %s %s %s want %s got %s flags %s" % (k, name, cid, want, g, flags))
    for r in rules:
        if r[4] == 0:
            bad.append("allow line matches nothing: %s %s %s" % tuple(r[:3]))
    for k in sorted(count):
        print("%6d %s" % (count[k], k))
    for b in bad:
        print(b)
    acc = sum(v for k, v in count.items() if k.startswith("explained accept"))
    rej = sum(v for k, v in count.items() if k.startswith("explained reject"))
    sk = sum(v for k, v in count.items() if k.startswith("skipped"))
    unexpl = sum(1 for b in bad if b.startswith("UNEXPLAINED"))
    print("LIMBO %s cases=%d agree=%d explained_accept=%d explained_reject=%d skipped=%d unexplained=%d"
          % ("FAIL" if bad else "PASS", n, count["agree"], acc, rej, sk, unexpl))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
