#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Compare an ACVP response against ACVP-Server's expectedResults.json, field by field.

usage: acvpcmp.py EXPECTED RESPONSE MAP LABEL [--subset] [--flip]
Every expected test must be either answered in the response or listed as not applicable in the map
(and then absent from the response). Answered tests must carry exactly the expected result fields
(--subset: only the fields the response carries, each of which must exist in the expected test) with
equal values; hex compares case-insensitively. Prints one line:
  ACVP LABEL PASS tests=N not_applicable=M      (exit 0)
  ACVP LABEL FAIL tests=N fail=K not_applicable=M (exit 1), first mismatches on stderr.
--flip is the negative control: one result value of the first answered expected test is corrupted in
memory (first hex digit changed, or a boolean inverted) before comparing; the run must then FAIL.
"""
import json
import sys


def body(path):
    j = json.load(open(path))
    return j[1] if isinstance(j, list) else j


def same(a, b):
    if isinstance(a, str) and isinstance(b, str):
        return a.upper() == b.upper()
    if isinstance(a, list) and isinstance(b, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    if isinstance(a, dict) and isinstance(b, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    return type(a) is type(b) and a == b


def corrupt(v):
    if isinstance(v, bool):
        return not v
    if isinstance(v, str):
        return ("1" if v[:1] == "0" else "0") + v[1:]
    if isinstance(v, list):
        return [corrupt(v[0])] + v[1:]
    if isinstance(v, dict):
        k = sorted(v)[0]
        return dict(v, **{k: corrupt(v[k])})
    raise SystemExit(f"cannot corrupt {v!r}")


def main():
    exp_path, resp_path, map_path, label = sys.argv[1:5]
    subset = "--subset" in sys.argv[5:]
    exp, resp, m = body(exp_path), body(resp_path), json.load(open(map_path))
    na = {(n[0], n[1]) for n in m["na"]}
    got = {(g["tgId"], t["tcId"]): t for g in resp["testGroups"] for t in g["tests"]}
    if "--flip" in sys.argv[5:]:
        g, t = next((g, t) for g in exp["testGroups"] for t in g["tests"] if (g["tgId"], t["tcId"]) in got)
        k = sorted(f for f in t if f != "tcId" and f in got[(g["tgId"], t["tcId"])])[0]
        t[k] = corrupt(t[k])
    ok = bad = skipped = 0
    msgs = []
    for k in ("vsId", "algorithm", "revision"):
        if exp.get(k) != resp.get(k):
            bad += 1
            msgs.append(f"header {k}: expected {exp.get(k)} got {resp.get(k)}")
    seen = set()
    for g in exp["testGroups"]:
        for t in g["tests"]:
            key = (g["tgId"], t["tcId"])
            want = {k: v for k, v in t.items() if k != "tcId"}
            have = got.get(key)
            if key in na and not subset:
                skipped += 1
                if have is not None:
                    bad += 1
                    msgs.append(f"tg {key[0]} tc {key[1]}: answered but listed not applicable")
                continue
            if have is None:
                if subset:
                    continue
                bad += 1
                msgs.append(f"tg {key[0]} tc {key[1]}: no response")
                continue
            seen.add(key)
            have = {k: v for k, v in have.items() if k != "tcId"}
            if subset:
                want = {k: want[k] for k in have if k in want}
            if same(want, have):
                ok += 1
            else:
                bad += 1
                diff = [k for k in sorted(set(want) | set(have)) if not same(want.get(k), have.get(k))]
                msgs.append(f"tg {key[0]} tc {key[1]}: fields {diff} differ"
                            + (f" ({have['error']})" if "error" in have else ""))
    extra = set(got) - seen
    if extra:
        bad += len(extra)
        msgs.append(f"{len(extra)} response tests not in expectedResults")
    if ok == 0 and bad == 0:
        bad += 1
        msgs.append("nothing compared")
    for s in msgs[:5]:
        print(f"  {label}: {s}", file=sys.stderr)
    verdict = "PASS" if bad == 0 else "FAIL"
    tail = f" fail={bad}" if bad else ""
    print(f"ACVP {label} {verdict} tests={ok}{tail} not_applicable={skipped}")
    sys.exit(0 if bad == 0 else 1)


if __name__ == "__main__":
    main()
