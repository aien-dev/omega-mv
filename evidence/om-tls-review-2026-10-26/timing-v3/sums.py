#!/usr/bin/env python3
"""Count and sum timing samples per class; no statistics (Wolfram computes those).

usage: sums.py FILE...   (each file: lines "CLASS NANOSECONDS", in measurement order)
Prints one JSON object: file -> {"n": [n0, n1], "tests": {name: [[n0,s0,q0],[n1,s1,q1]], ...},
"thr": {name: threshold_ns}, "bad": malformed_line_count}.
Tests: "full"; "p90" (pooled 90th percentile crop, the v1/v2 convention); "dP" for the dudect
crop set p = 1 - 0.5^(10 (i+1) / 100), i in DUDECT_I (dudect's prepare_percentiles), threshold
taken from the pooled sorted samples, keeping samples <= threshold.
A malformed line or a class other than 0/1 is counted in "bad" and makes the script exit 1.
"""
import json, sys

DUDECT_I = (0, 9, 19, 29, 39, 49, 59, 69, 79, 89, 99)


def crop_sets(ts):
    srt = sorted(ts)
    def at(p):
        return srt[min(len(srt) - 1, int(p * len(srt)))]
    out = {"p90": at(0.90)}
    for i in DUDECT_I:
        out["d%d" % i] = at(1 - 0.5 ** (10 * (i + 1) / 100))
    return out


def main():
    res, rc = {}, 0
    for f in sys.argv[1:]:
        xs, bad = [], 0
        for line in open(f):
            p = line.split()
            if len(p) != 2 or p[0] not in ("0", "1") or not p[1].isdigit():
                bad += 1
                continue
            xs.append((int(p[0]), int(p[1])))
        if bad or not xs:
            rc = 1
        thr = crop_sets([t for _, t in xs]) if xs else {}
        tests = {}
        for name, lim in [("full", None)] + sorted(thr.items()):
            row = []
            for c in (0, 1):
                v = [t for k, t in xs if k == c and (lim is None or t <= lim)]
                row.append([len(v), sum(v), sum(t * t for t in v)])
            tests[name] = row
        res[f] = {"n": [sum(1 for k, _ in xs if k == 0), sum(1 for k, _ in xs if k == 1)],
                  "tests": tests, "thr": thr, "bad": bad}
    json.dump(res, sys.stdout, indent=1, sort_keys=True)
    print()
    sys.exit(rc)


if __name__ == "__main__":
    main()
