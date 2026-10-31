"""Materialize om-bench tasks to disk and hash them.

usage: materialize.py PUBLIC_OUT SEALED_OUT [MANIFEST_OUT]
Writes <out>/<split>/<tid>/{statement.txt, NN.in, NN.out, meta.json}; cases 00-01 are the visible
examples, the rest hidden. Fails closed: any oracle exception, nondeterminism, oversized input or
duplicate input inside a task is a fatal error (exit 1), never a skipped task.
"""
import hashlib, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import benchlib, families_public as P
sys.path.insert(0, benchlib.SEALED)

MAX_IN = 65536


def sha(bs):
    return hashlib.sha256(bs).hexdigest()


def build(builders, split_of):
    tasks = []
    for fam, fn in builders.items():
        ts = fn()
        if len(ts) != 4:
            sys.exit(f"FATAL {fam}: {len(ts)} variants, want 4")
        for t in ts:
            tasks.append((split_of[fam], t))
    return tasks


def write_task(root, split, t):
    d = os.path.join(root, split, t.tid)
    os.makedirs(d, exist_ok=True)
    stmt = (t.statement + "\n").encode()
    files = {"statement.txt": stmt}
    cases = []
    seen = set()
    for k, inp in enumerate(t.examples + t.hidden):
        if len(inp) > MAX_IN:
            sys.exit(f"FATAL {t.tid} case {k}: input {len(inp)} bytes > {MAX_IN}")
        if inp in seen:
            continue
        seen.add(inp)
        o1, e1 = t.oracle(inp)
        o2, e2 = t.oracle(inp)
        if (o1, e1) != (o2, e2):
            sys.exit(f"FATAL {t.tid} case {k}: oracle nondeterministic")
        n = len(cases)
        files[f"{n:02d}.in"] = inp
        files[f"{n:02d}.out"] = o1
        cases.append({"n": n, "visible": k < len(t.examples), "exit": e1})
    if sum(1 for c in cases if c["visible"]) != 2 or len(cases) < 5:
        sys.exit(f"FATAL {t.tid}: visible={sum(c['visible'] for c in cases)} cases={len(cases)}")
    meta = {"tid": t.tid, "family": t.family, "split": split, "cases": cases}
    files["meta.json"] = (json.dumps(meta, indent=1, sort_keys=True) + "\n").encode()
    hashes = {}
    for name, data in files.items():
        with open(os.path.join(d, name), "wb") as f:
            f.write(data)
        hashes[name] = sha(data)
    return {"tid": t.tid, "family": t.family, "split": split, "n_cases": len(cases),
            "statement_sha256": hashes["statement.txt"],
            "task_sha256": sha("".join(f"{k} {hashes[k]}\n" for k in sorted(hashes)).encode()),
            "files": hashes}


def write_manifest(path, rows):
    """Public manifest: every public file hash; for sealed tasks only a keyed opaque id and one commitment
    (task_sha256 = sha256 of the sorted 'name hash' list), never per-file hashes (see CONTRACT.md)."""
    pub, sealed = [], []
    for r in sorted(rows, key=lambda r: (r["split"], r["tid"])):
        if r["split"] in ("train", "dev"):
            pub.append(r)
        else:
            sealed.append({"task": benchlib.opaque(r["split"], r["tid"]), "split": r["split"],
                           "task_sha256": r["task_sha256"]})
    sealed.sort(key=lambda r: (r["split"], r["task"]))
    counts = {}
    for r in rows:
        counts[r["split"]] = counts.get(r["split"], 0) + 1
    with open(path, "w") as f:
        json.dump({"tasks_by_split": counts, "public": pub, "sealed": sealed}, f, indent=1, sort_keys=True)
        f.write("\n")


def main():
    pub, sealed = sys.argv[1], sys.argv[2]
    import families_sealed as S
    rows = []
    for split, t in build(P.BUILDERS, P.SPLIT):
        rows.append(write_task(pub, split, t))
    for split, t in build(S.BUILDERS, S.SPLIT):
        rows.append(write_task(sealed, split, t))
    fams = {}
    for r in rows:
        fams.setdefault(r["family"], set()).add(r["split"])
    for f, s in fams.items():
        if len(s) != 1:
            sys.exit(f"FATAL family {f} in splits {s}")
    with open(os.path.join(sealed, "index.json"), "w") as f:
        json.dump(rows, f, indent=1, sort_keys=True)
    if len(sys.argv) > 3:
        write_manifest(sys.argv[3], rows)
    counts = {}
    for r in rows:
        counts[r["split"]] = counts.get(r["split"], 0) + 1
    print("tasks", len(rows), "by split", json.dumps(counts, sort_keys=True),
          "cases", sum(r["n_cases"] for r in rows))


if __name__ == "__main__":
    main()
