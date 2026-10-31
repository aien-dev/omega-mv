#!/usr/bin/env python3
"""om-bench (#653) near-duplicate and renamed-leak check across splits (disclosed host tool).

Statements are normalised (lower case, every number -> N, a synonym map folds common rewordings,
punctuation and stop words dropped). Three similarities per cross-family pair: word-set Jaccard,
content-word overlap coefficient |A&B|/min(|A|,|B|), and character 5-gram Jaccard. A pair is FLAGGED
by rule(): word >= 0.6, or char5 >= 0.6, or (overlap >= 0.9 and word >= 0.45). Flagged pairs are a
screening list: each is reviewed by hand and recorded in leak-reviews.json with a verdict (duplicate /
template / same-split-overlap). Exit 1 if a flag has no review or a cross-split pair is a duplicate. Same-family pairs (variants of one family, always one
split) are expected to be similar and are not flagged.

Planted controls: PLANT1 (a dev statement reworded) MUST be flagged, otherwise exit 1. The v1 normaliser
(no stop words, no overlap coefficient; word or char5 >= 0.6) missed PLANT1 (0.54 word); v2 was tuned
after seeing PLANT1, so PLANT1 is not a blind control. A pure overlap>=0.6 rule flagged 807 pairs and was
rejected as useless for review. PLANT2 (a heavy paraphrase of a train statement) is reported, not required:
it measures the limit of a lexical check. Sealed task ids are printed only as keyed opaque ids
(benchlib.opaque: HMAC-SHA256 under a private key, so family names cannot be recovered by guessing).
Limits: lexical only; a leak paraphrased beyond the synonym map, or one that reuses a solution rather
than a statement, is not detected.
"""
import itertools, json, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from benchlib import opaque

THRESH = 0.6
SYN = {"output": "print", "write": "print", "display": "print", "emit": "print", "integer": "number",
       "integers": "numbers", "value": "number", "values": "numbers", "given": "read", "input": "read",
       "obtain": "read", "count": "number", "amount": "number", "string": "line", "text": "line",
       "whether": "if", "otherwise": "else", "below": "under", "less": "under", "parentheses": "bracket",
       "brackets": "bracket", "parenthesis": "bracket", "possibly": "maybe", "properly": "", "when": "if",
       "places": "positions", "position": "positions", "move": "shift", "shifted": "shift",
       "characters": "character", "chars": "character", "bytes": "byte", "letters": "letter"}
STOP = {"a", "an", "the", "one", "of", "and", "or", "is", "are", "it", "its", "be", "by", "to", "in", "on",
        "for", "with", "that", "this", "they", "not", "if", "else", "made", "only", "each", "every", "all",
        "as", "at", "from", "so", "then", "n", "maybe", "when", "there", "which", ""}


def norm(s):
    s = re.sub(r"\d+", " N ", s.lower())
    s = re.sub(r"[^a-z ]", " ", s)
    return [SYN.get(w, w) for w in s.split()]


def content(ws):
    return {w for w in ws if w not in STOP}


def grams(ws, k=5):
    t = " ".join(ws)
    return {t[i:i + k] for i in range(max(1, len(t) - k + 1))}


def jac(a, b):
    return len(a & b) / len(a | b) if (a or b) else 1.0


def ovl(a, b):
    return len(a & b) / min(len(a), len(b)) if (a and b) else 0.0


def load(root):
    out = []
    for split in sorted(os.listdir(root)):
        d = os.path.join(root, split)
        if not os.path.isdir(d):
            continue
        for tid in sorted(os.listdir(d)):
            p = os.path.join(d, tid, "statement.txt")
            if os.path.isfile(p):
                out.append((split, tid, open(p).read().strip()))
    return out



PLANT1 = ("Read a line made only of ( and ) characters, maybe empty. Output YES when the brackets are "
          "properly balanced and NO when they are not.")
PLANT2 = ("Move each alphabet character 7 positions forward, so y becomes f, keep upper and lower case, "
          "and pass every other byte through untouched.")


def rule(sw, so, sg):
    return sw >= THRESH or sg >= THRESH or (so >= 0.9 and sw >= 0.45)


def main():
    pub, sealed = sys.argv[1], sys.argv[2]
    rpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), "leak-reviews.json")
    reviews = json.load(open(rpath)) if os.path.exists(rpath) else {}
    rows = load(pub) + load(sealed)
    n_real = len(rows)
    rows += [("heldA", "zz-plant1.v1", PLANT1), ("heldA", "zz-plant2.v1", PLANT2)]
    feats = []
    for s, t, x in rows:
        ws = norm(x)
        feats.append((s, t, set(ws), content(ws), grams(ws)))
    flags, best, plant = [], 0.0, {"zz-plant1": 0.0, "zz-plant2": 0.0}
    for (s1, t1, w1, c1, g1), (s2, t2, w2, c2, g2) in itertools.combinations(feats, 2):
        f1, f2 = t1.split(".")[0], t2.split(".")[0]
        if f1 == f2:
            continue
        sw, so, sg = jac(w1, w2), ovl(c1, c2), jac(g1, g2)
        m = max(sw, so, sg)
        pz = [f for f in (f1, f2) if f.startswith("zz-")]
        if pz:
            if len(pz) == 1 and rule(sw, so, sg):
                plant[pz[0]] = 1.0
            continue
        best = max(best, m)
        if rule(sw, so, sg):
            flags.append({"a": opaque(s1, t1), "b": opaque(s2, t2), "word": round(sw, 3),
                          "content_overlap": round(so, 3), "char5": round(sg, 3), "cross_split": s1 != s2})
            key = flags[-1]["a"] + " | " + flags[-1]["b"]
            flags[-1]["review"] = reviews.get(key)
    res = {"version": 2, "tasks": n_real, "pairs_checked": n_real * (n_real - 1) // 2, "threshold": THRESH,
           "max_cross_family_similarity": round(best, 3), "n_flags": len(flags),
           "n_flags_cross_split": sum(f["cross_split"] for f in flags), "flags": flags,
           "plant1_flagged": plant["zz-plant1"] > 0, "plant2_flagged": plant["zz-plant2"] > 0}
    print(json.dumps(res, indent=1))
    if not res["plant1_flagged"]:
        print("LEAK CONTROL FAIL planted duplicate not flagged")
        sys.exit(1)
    unreviewed = [f for f in flags if not f["review"]]
    dup = [f for f in flags if f["cross_split"] and (f["review"] or "").startswith("duplicate")]
    for f in unreviewed:
        print("LEAK UNREVIEWED", f["a"], "|", f["b"])
    for f in dup:
        print("LEAK DUPLICATE ACROSS SPLITS", f["a"], "|", f["b"])
    sys.exit(1 if unreviewed or dup else 0)


if __name__ == "__main__":
    main()
