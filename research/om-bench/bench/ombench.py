#!/usr/bin/env python3
"""om-bench (#653) driver: generation arms, candidate execution, comparator, denominator guard.

Disclosed host tool (Python 3.12, outside the Omega build closure). The Omega arms are additionally
judged by the seeded .om runner seed/qualification/om2/gate.om; `crosscheck` requires both judges to
agree case by case, so this comparator is never the only judge of an Omega candidate.

  ombench.py gen   --arm {a,b,aeq,fpy,fc} --split SPLIT --run DIR [--tasks T1,T2]
  ombench.py judge --run DIR --split SPLIT          # run final candidates on every case
  ombench.py gatelist --run DIR --split SPLIT       # gate.om case list for Omega candidates
  ombench.py crosscheck --run DIR --gate-log LOG    # agree with gate.om verdicts or exit 1
  ombench.py summary --run DIR                       # aggregate counts (no task contents)
"""
import argparse, ast, hashlib, json, os, re, shutil, signal, subprocess, sys, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.environ.get("OMB_REPO", os.path.abspath(os.path.join(HERE, "..", "..", "..")))
sys.path.insert(0, HERE)
from benchlib import SEALED
OMC = os.path.join(REPO, "seed/out/omc")
OMC_SHA = "2c82480596bbdc50cda9626866209ef662d5834f278f839ff4ce0250c495029f"
MAX_TOKENS = 2048
LLM = os.environ.get("OMB_LLM", "http://127.0.0.1:18653")
TASKROOTS = ([os.environ["OMB_TASKROOT"]] if os.environ.get("OMB_TASKROOT") else
             [os.path.join(HERE, "pubtasks"), os.path.join(SEALED, "tasks")])
RUN_DEADLINE = 10.0
COMPILE_DEADLINE = 60.0
REPAIRS = 3
AEQ_SAMPLES = 4
SEED = 653
CPU_CORES = 2  # llama-server: taskset -c 3-4, -t 2 (cores shared on the box; see CONTRACT.md section 4)
CLUSTERS = os.path.join(SEALED, "clusters.json")

ERRMEAN = {
    1: "compiler I/O", 2: "compiler I/O", 3: "unknown name or field", 4: "line ends too early",
    5: "extra words on the line", 6: "malformed number", 7: "type mismatch (no implicit conversions; "
    "both operands, a name and its value, an argument and its parameter must have the same type)",
    8: "number out of range", 9: "word in a disallowed place", 10: "duplicate name",
    11: "ambiguous bare number: give it a type, e.g. ulong 0", 12: "too many live names or temporaries "
    "(16 registers): split the function", 13: "non-function name longer than 8 characters",
    14: "capacity", 15: "missing end", 16: "syntax error or unknown word", 17: "line after ret, break, "
    "continue, trap or exit in the same block", 18: "missing ret on some path", 19: "global passed as an "
    "argument, or the same array twice in one call", 20: "write to a view or text",
    21: "constant index out of range", 22: "bad text literal", 23: "size capacity",
}

COMMON_IO = None


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def sha_file(p):
    with open(p, "rb") as f:
        return sha256(f.read())


def load_common_io():
    sys.path.insert(0, HERE)
    import benchlib
    return benchlib.COMMON_IO


def task_dirs(split):
    for root in TASKROOTS:
        d = os.path.join(root, split)
        if os.path.isdir(d):
            return d, sorted(os.listdir(d))
    sys.exit(f"FATAL split {split}: no task directory")


def fault(tid, reason):
    print(f"CORPUS FAULT [{tid}] {reason}")
    sys.exit(1)


def load_task(split, tid):
    d, _ = task_dirs(split)
    td = os.path.join(d, tid)
    for f in ("meta.json", "statement.txt"):
        if not os.path.isfile(os.path.join(td, f)):
            fault(tid, f"missing {f}")
    meta = json.load(open(os.path.join(td, "meta.json")))
    stmt = open(os.path.join(td, "statement.txt")).read().strip()
    if "cases" not in meta or not meta["cases"]:
        fault(tid, "no cases")
    if not stmt:
        fault(tid, "empty statement")
    cases = []
    for c in meta["cases"]:
        for k in ("n", "visible", "exit"):
            if k not in c or c[k] is None:
                fault(tid, f"case missing {k}")
        n = c["n"]
        cases.append({"n": n, "visible": c["visible"], "exit": c["exit"],
                      "in": os.path.join(td, f"{n:02d}.in"), "out": os.path.join(td, f"{n:02d}.out")})
        for k in ("in", "out"):
            if not os.path.isfile(cases[-1][k]):
                fault(tid, f"case {n} missing .{k}")
    if sum(1 for c in cases if c["visible"]) != 2:
        fault(tid, "visible example count is not 2")
    return {"tid": tid, "dir": td, "statement": stmt, "cases": cases, "family": meta.get("family")}


def host_words():
    src = open(os.path.join(REPO, "seed/omc/genkw.awk")).read()
    words = set()
    for m in re.finditer(r"([a-z0-9]+):(\d+)", src):
        if int(m.group(2)) >= 100:
            words.add(m.group(1))
    if len(words) < 50:
        sys.exit("FATAL host word table not parsed")
    return words


LANG = {
    "om": ("Omega", "om"),
    "py": ("Python 3", "python"),
    "c": ("C (C11, compiled with gcc -O2, only the standard library)", "c"),
}


def system_prompt(lang):
    if lang == "om":
        card = open(os.path.join(HERE, "..", "REFERENCE-CARD.md")).read()
        return ("You write complete Omega programs. Follow this reference card exactly.\n\n" + card)
    return f"You write complete {LANG[lang][0]} programs."


def user_prompt(task, lang):
    ex = []
    for c in task["cases"]:
        if c["visible"]:
            i = open(c["in"], "rb").read().decode("ascii")
            o = open(c["out"], "rb").read().decode("ascii")
            ex.append(f"Example input:\n```\n{i}```\nExpected output:\n```\n{o}```")
    fence = LANG[lang][1]
    return (f"Task: {task['statement']}\n\nInput and output rules: {COMMON_IO}\n\n" + "\n\n".join(ex) +
            f"\n\nWrite one complete {LANG[lang][0]} program that solves the task for every valid input, "
            f"not only the examples. Reply with the program in a single ```{fence} code block.")


def chat(messages, temperature, seed):
    body = {"messages": messages, "temperature": temperature, "seed": seed, "max_tokens": MAX_TOKENS,
            "cache_prompt": True, "stream": False}
    if temperature == 0:
        body["top_k"] = 1
    req = urllib.request.Request(LLM + "/v1/chat/completions", data=json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=3600) as r:
        resp = json.loads(r.read())
    wall = time.time() - t0
    msg = resp["choices"][0]["message"]["content"]
    u = resp.get("usage", {})
    t = resp.get("timings", {})
    return msg, {"wall_s": round(wall, 3), "prompt_tokens": u.get("prompt_tokens"),
                 "completion_tokens": u.get("completion_tokens"), "prompt_n_processed": t.get("prompt_n"),
                 "prompt_ms": t.get("prompt_ms"), "predicted_ms": t.get("predicted_ms"),
                 "finish_reason": resp["choices"][0].get("finish_reason"), "model": resp.get("model"),
                 "system_fingerprint": resp.get("system_fingerprint")}


FENCE = re.compile(r"```[A-Za-z0-9_+-]*[ \t]*\n(.*?)(```|\Z)", re.S)


def extract(text):
    m = FENCE.search(text)
    if not m:
        return None
    code = m.group(1)
    if code and not code.endswith("\n"):
        code += "\n"
    return code


def compile_om(src_path, out_path):
    with open(src_path, "rb") as fi, open(out_path, "wb") as fo:
        try:
            p = subprocess.run([OMC], stdin=fi, stdout=fo, stderr=subprocess.PIPE, timeout=COMPILE_DEADLINE)
        except subprocess.TimeoutExpired:
            return 124, "compile timeout"
    os.chmod(out_path, 0o700)
    return p.returncode, p.stderr.decode(errors="replace")


def compile_c(src_path, out_path):
    try:
        p = subprocess.run(["gcc", "-O2", "-std=gnu11", "-o", out_path, src_path, "-lm"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=COMPILE_DEADLINE)
    except subprocess.TimeoutExpired:
        return 124, "compile timeout"
    return p.returncode, p.stdout.decode(errors="replace")


def compile_py(src_path, _out):
    p = subprocess.run([sys.executable, "-I", "-c",
                        "import sys,ast;ast.parse(open(sys.argv[1]).read())", src_path],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode(errors="replace")


def build(lang, src_path, workdir):
    out = os.path.join(workdir, "prog")
    if lang == "om":
        rc, err = compile_om(src_path, out)
    elif lang == "c":
        rc, err = compile_c(src_path, out)
    else:
        rc, err = compile_py(src_path, out)
    return rc, err


PY_OK_MODULES = {"sys", "math", "collections", "itertools", "functools", "heapq", "bisect", "string", "re",
                 "fractions", "decimal", "operator", "array", "statistics", "typing", "dataclasses", "copy"}
PY_BAD_CALLS = {"open", "exec", "eval", "compile", "__import__", "breakpoint", "globals", "vars"}
PY_BAD = re.compile(r"\b(import|from)\b|open\s*\(|exec\s*\(|eval\s*\(|__import__")


def py_scan(code):
    """Python: imports outside an allowlist, or file/exec calls. Falls back to a coarse regex when the
    candidate does not parse (it would fail at run time anyway, but is still not executed if flagged)."""
    try:
        tree = ast.parse(code)
    except (SyntaxError, ValueError):
        return sorted(set(m.group(0).strip() for m in PY_BAD.finditer(code)
                          if not m.group(0).startswith(("import", "from"))))
    found = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            found += ["import " + a.name for a in node.names if a.name.split(".")[0] not in PY_OK_MODULES]
        elif isinstance(node, ast.ImportFrom):
            if (node.module or "").split(".")[0] not in PY_OK_MODULES:
                found.append("from " + (node.module or "."))
        elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in PY_BAD_CALLS:
            found.append(node.func.id + "(")
        elif isinstance(node, ast.Attribute) and node.attr in ("system", "popen", "fork", "remove", "unlink"):
            found.append("." + node.attr)
    return sorted(set(found))


C_BAD = re.compile(r"\b(fopen|open|system|popen|fork|exec[lv]p?e?|socket|connect|unlink|remove|rename|"
                   r"mmap|ptrace|kill|dlopen|syscall)\s*\(|#\s*include\s*<(sys/socket|netinet/|arpa/|"
                   r"dlfcn|sys/ptrace)")


def authz_scan(lang, code, hostw):
    """Static scan: candidates may touch only stdin, stdout and exit. Returns list of findings."""
    if lang == "om":
        found = []
        for ln in code.split("\n"):
            toks = ln.split()
            if toks and toks[0] == "note":
                continue
            for t in toks:
                if t in hostw:
                    found.append(t)
            # fds other than 0 (read) / 1 (write)
            for k, t in enumerate(toks):
                if t in ("read", "pread") and k + 1 < len(toks) and toks[k + 1] != "0":
                    found.append(f"{t}-fd:{toks[k + 1]}")
                if t in ("write", "pwrite") and k + 1 < len(toks) and toks[k + 1] not in ("1", "2"):
                    found.append(f"{t}-fd:{toks[k + 1]}")
        return sorted(set(found))
    if lang == "py":
        return py_scan(code)
    return sorted(set(m.group(0).strip() for m in C_BAD.finditer(code)))


def run_case(lang, workdir, case):
    prog = os.path.join(workdir, "prog")
    if lang == "py":
        cmd = [sys.executable, "-I", os.path.join(workdir, "prog.py")]
    else:
        cmd = [prog]
    exe = os.path.join(workdir, "exec")
    os.makedirs(exe, exist_ok=True)
    with open(case["in"], "rb") as fi:
        t0 = time.time()
        try:
            p = subprocess.Popen(cmd, stdin=fi, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=exe,
                                 start_new_session=True, env={"PATH": "/usr/bin:/bin", "LANG": "C"},
                                 preexec_fn=lambda: __import__("resource").setrlimit(
                                     __import__("resource").RLIMIT_FSIZE, (1 << 24, 1 << 24)))
        except OSError as e:
            return {"status": "exec_fail", "detail": str(e)}
        try:
            so, se = p.communicate(timeout=RUN_DEADLINE)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
            return {"status": "timeout", "wall_s": RUN_DEADLINE}
        wall = time.time() - t0
    want = open(case["out"], "rb").read()
    rc = p.returncode
    r = {"rc": rc, "wall_s": round(wall, 4), "stdout_sha256": sha256(so), "stdout_len": len(so)}
    if rc < 0:
        r["status"] = "signal"
    elif so == want and rc == case["exit"]:
        r["status"] = "pass"
    elif rc != case["exit"]:
        r["status"] = "trap" if (lang == "om" and 100 <= rc <= 114) else "bad_exit"
    elif len(so) == 0 and len(want) > 0:
        r["status"] = "empty_output"
    else:
        r["status"] = "wrong_output"
    return r


MECH_CASE = {"exec_fail", "timeout", "signal", "trap", "bad_exit", "empty_output"}


def classify(rec):
    """Task-level class. Mechanical = needs a hand repair of syntax, wiring, target code or integration."""
    if rec.get("authz"):
        return "authz_violation"
    if rec["code_sha256"] is None:
        return "no_code"
    if rec["compile_rc"] != 0:
        return "compile_refused"
    sts = [c["status"] for c in rec["cases"]]
    if all(s == "pass" for s in sts):
        return "pass"
    if any(s in MECH_CASE for s in sts):
        return "runtime_mechanical"
    return "wrong_output"


def arm_lang(arm):
    return {"a": "om", "b": "om", "aeq": "om", "fpy": "py", "fc": "c"}[arm]


def src_name(lang):
    return {"om": "prog.om", "py": "prog.py", "c": "prog.c"}[lang]


def write_candidate(workdir, lang, code):
    os.makedirs(workdir, exist_ok=True)
    p = os.path.join(workdir, src_name(lang))
    with open(p, "w") as f:
        f.write(code)
    return p


def feedback_text(err, code):
    m = re.search(r"line (\d+): error (\d+)", err)
    if not m:
        return f"The compiler refused the program. Its message was:\n{err.strip()}\nReply with the corrected complete program in a single ```om code block."
    ln, k = int(m.group(1)), int(m.group(2))
    lines = code.split("\n")
    src = lines[ln - 1] if 0 < ln <= len(lines) else ""
    return (f"The Omega compiler refused the program: `omc: line {ln}: error {k}` ({ERRMEAN.get(k, 'see card')}). "
            f"Line {ln} is: `{src}`\nFix it and reply with the corrected complete program in a single ```om code block.")


def gen_task(arm, task, rundir, log):
    lang = arm_lang(arm)
    tdir = os.path.join(rundir, "cand", task["tid"])
    if os.path.exists(os.path.join(tdir, "gen.json")):
        return
    os.makedirs(tdir, exist_ok=True)
    sysm = {"role": "system", "content": system_prompt(lang)}
    usr = {"role": "user", "content": user_prompt(task, lang)}
    attempts = []
    final_code = None
    if arm in ("a", "fpy", "fc"):
        text, meta = chat([sysm, usr], 0, SEED)
        attempts.append({"k": 0, "reply_sha256": sha256(text.encode()), "reply": text, **meta})
        final_code = extract(text)
    elif arm == "b":
        msgs = [sysm, usr]
        for k in range(REPAIRS + 1):
            text, meta = chat(msgs, 0, SEED)
            code = extract(text)
            at = {"k": k, "reply_sha256": sha256(text.encode()), "reply": text, **meta}
            final_code = code
            if code is None:
                fb = "Your reply had no code block. Reply with the complete program in a single ```om code block."
            else:
                wd = os.path.join(tdir, f"try{k}")
                sp = write_candidate(wd, lang, code)
                rc, err = build(lang, sp, wd)
                at["compile_rc"], at["compile_err"] = rc, err[:2000]
                if rc == 0:
                    attempts.append(at)
                    break
                fb = feedback_text(err, code)
            at["feedback"] = fb
            attempts.append(at)
            msgs = msgs + [{"role": "assistant", "content": text}, {"role": "user", "content": fb}]
    elif arm == "aeq":
        for k in range(AEQ_SAMPLES):
            text, meta = chat([sysm, usr], 0.8, SEED + 1 + k)
            code = extract(text)
            at = {"k": k, "reply_sha256": sha256(text.encode()), "reply": text, **meta}
            final_code = code
            if code is not None:
                wd = os.path.join(tdir, f"try{k}")
                sp = write_candidate(wd, lang, code)
                rc, err = build(lang, sp, wd)
                at["compile_rc"] = rc
                attempts.append(at)
                if rc == 0:
                    break
            else:
                attempts.append(at)
    if final_code is not None:
        write_candidate(tdir, lang, final_code)
    rec = {"tid": task["tid"], "arm": arm, "lang": lang, "attempts": attempts,
           "code_sha256": sha256(final_code.encode()) if final_code is not None else None,
           "llm_wall_s": round(sum(a["wall_s"] for a in attempts), 3),
           "prompt_tokens": sum(a["prompt_tokens"] or 0 for a in attempts),
           "prompt_tokens_processed": sum(a["prompt_n_processed"] or 0 for a in attempts),
           "completion_tokens": sum(a["completion_tokens"] or 0 for a in attempts)}
    with open(os.path.join(tdir, "gen.json"), "w") as f:
        json.dump(rec, f, indent=1)
    log.write(json.dumps({"tid": task["tid"], "arm": arm, "attempts": len(attempts),
                          "completion_tokens": rec["completion_tokens"], "llm_wall_s": rec["llm_wall_s"],
                          "t": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}) + "\n")
    log.flush()


def judge(rundir, split, arm):
    lang = arm_lang(arm)
    hostw = host_words()
    _, tids = task_dirs(split)
    out = []
    for tid in tids:
        task = load_task(split, tid)
        tdir = os.path.join(rundir, "cand", tid)
        gp = os.path.join(tdir, "gen.json")
        if not os.path.exists(gp):
            out.append({"tid": tid, "class": "NOT_RUN"})
            continue
        gen = json.load(open(gp))
        rec = {"tid": tid, "code_sha256": gen["code_sha256"], "compile_rc": None, "cases": [], "authz": []}
        if gen["code_sha256"] is not None:
            code = open(os.path.join(tdir, src_name(lang))).read()
            rec["authz"] = authz_scan(lang, code, hostw)
            jd = os.path.join(tdir, "judge")
            if os.path.exists(jd):
                shutil.rmtree(jd)
            os.makedirs(jd)
            sp = write_candidate(jd, lang, code)
            t0 = time.time()
            rc, err = build(lang, sp, jd)
            rec["compile_s"] = round(time.time() - t0, 3)
            rec["compile_rc"], rec["compile_err"] = rc, err[:500]
            if rc == 0 and not rec["authz"]:
                if lang != "py":
                    rec["bin_sha256"] = sha_file(os.path.join(jd, "prog"))
                for c in task["cases"]:
                    r = run_case(lang, jd, c)
                    r["n"], r["visible"] = c["n"], c["visible"]
                    rec["cases"].append(r)
        rec["class"] = classify(rec)
        rec["n_cases"] = len(task["cases"])
        out.append(rec)
    missing = [r["tid"] for r in out if r["class"] == "NOT_RUN"]
    with open(os.path.join(rundir, f"verdicts-{split}.jsonl"), "w") as f:
        for r in out:
            f.write(json.dumps(r, sort_keys=True) + "\n")
    if len(out) != len(tids):
        sys.exit(f"DENOMINATOR FAULT verdicts={len(out)} manifest={len(tids)}")
    if missing:
        print(f"NOT_RUN {len(missing)} of {len(tids)}: {' '.join(missing)}")
        sys.exit(2)
    print(f"judged {len(out)}/{len(tids)}", json.dumps(counts(out), sort_keys=True))


def counts(rows):
    c = {}
    for r in rows:
        c[r["class"]] = c.get(r["class"], 0) + 1
    return c


def gatelist(rundir, split):
    _, tids = task_dirs(split)
    lines = [f"deadline {int(RUN_DEADLINE)}"]
    for tid in tids:
        task = load_task(split, tid)
        src = os.path.join(rundir, "cand", tid, "prog.om")
        if not os.path.exists(src):
            lines.append(f"{tid}.nocode notrun no-code-block")
            continue
        for c in task["cases"]:
            lines.append(f"{tid}.{c['n']:02d} run {os.path.relpath(src, REPO)} file:{os.path.relpath(c['in'], REPO)} "
                         f"file:{os.path.relpath(c['out'], REPO)} any {c['exit']}")
    p = os.path.join(rundir, f"gate-{split}.cases")
    with open(p, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(p, len(lines) - 1)


def crosscheck(rundir, split, gate_log):
    verdict = {}
    log = open(gate_log, errors="replace").read()
    # A crash or setup failure can leave no verdicts, or only a passing prefix.
    # Candidate FAIL/NOT_RUN/INVALID outcomes remain visible, not process success.
    final = re.search(r"^OM2GATE (PASS|FAIL|INVALID|NOT_RUN) cases=(\d+) pass=(\d+) fail=(\d+) invalid=(\d+) not_run=(\d+)\n?\Z", log, re.M)
    if final is None or "MISMATCH: every case is NOT_RUN" in log:
        print("INVALID incomplete gate report or compiler identity mismatch")
        return 1
    for ln in log.splitlines():
        m = re.match(r"VERDICT (\S+) (PASS|FAIL|INVALID|NOT_RUN)\b", ln)
        if m:
            if m.group(1) in verdict:
                print("INVALID duplicate gate verdict")
                return 1
            verdict[m.group(1)] = m.group(2)
    reported = tuple(int(n) for n in final.groups()[1:])
    actual = (len(verdict), *(sum(v == kind for v in verdict.values())
                              for kind in ("PASS", "FAIL", "INVALID", "NOT_RUN")))
    if reported != actual or not verdict:
        print("INVALID gate report counts do not match verdicts")
        return 1
    outcome = ("INVALID" if actual[3] else "FAIL" if actual[2] else
               "NOT_RUN" if actual[4] else "PASS")
    if final.group(1) != outcome:
        print("INVALID gate outcome does not match verdicts")
        return 1
    rows = [json.loads(l) for l in open(os.path.join(rundir, f"verdicts-{split}.jsonl"))]
    agree = disagree = 0
    bad = []
    expected = set()
    for r in rows:
        if r["code_sha256"] is None:
            name = f"{r['tid']}.nocode"
            expected.add(name)
            if verdict.get(name) == "NOT_RUN":
                agree += 1
            else:
                disagree += 1
                bad.append((name, "expected-no-code-NOT_RUN", verdict.get(name)))
            continue
        task = load_task(split, r["tid"])
        byn = {c["n"]: c["status"] for c in r["cases"]}
        for c in task["cases"]:
            name = f"{r['tid']}.{c['n']:02d}"
            expected.add(name)
            g = verdict.get(name)
            if g is None:
                bad.append((name, "missing-in-gate", byn.get(c["n"])))
                disagree += 1
                continue
            if r["class"] in ("compile_refused", "authz_violation"):
                ok = g != "PASS"
            else:
                ok = (byn.get(c["n"]) == "pass") == (g == "PASS")
            if ok:
                agree += 1
            else:
                disagree += 1
                bad.append((name, g, byn.get(c["n"])))
    if set(verdict) - expected:
        print("INVALID unexpected gate verdicts")
        return 1
    print(f"crosscheck agree={agree} disagree={disagree}")
    for b_ in bad[:20]:
        print("DISAGREE", *b_)
    return 1 if disagree else 0


def summary(rundir, split, arm):
    """Publishable aggregate: sealed task and family names only as keyed opaque ids (benchlib.opaque)."""
    if os.path.exists(os.path.join(rundir, f"summary-{split}.json")):
        sys.exit("REFUSED existing summary; preserve it and use a new run directory")
    if arm_lang(arm) == "om":
        if crosscheck(rundir, split, os.path.join(rundir, "gate.log")):
            sys.exit("INVALID Omega gate crosscheck; summary not written")
    sys.path.insert(0, HERE)
    from benchlib import opaque
    merge = {}
    if os.path.exists(CLUSTERS):
        for grp in json.load(open(CLUSTERS))["merge"]:
            for f in grp:
                merge[f] = "+".join(sorted(grp))
    _, tids = task_dirs(split)
    vp = os.path.join(rundir, f"verdicts-{split}.jsonl")
    rows = {r["tid"]: r for r in (json.loads(l) for l in open(vp))}
    if sorted(rows) != tids:
        sys.exit(f"DENOMINATOR FAULT verdicts={len(rows)} manifest={len(tids)}")
    out = []
    for tid in tids:
        task = load_task(split, tid)
        fam = task["family"]
        r = rows[tid]
        gp = os.path.join(rundir, "cand", tid, "gen.json")
        g = json.load(open(gp)) if os.path.exists(gp) else {}
        att = g.get("attempts", [])
        first = None
        if att and arm == "b":
            first = att[0].get("compile_rc") == 0
        cases = r.get("cases", [])
        out.append({"task": opaque(split, tid), "family": opaque(split, fam),
                    "cluster": opaque(split, merge.get(fam, fam)), "class": r["class"],
                    "attempts": len(att), "first_compiles": first,
                    "reply0_sha256": att[0]["reply_sha256"] if att else None,
                    "code_sha256": r.get("code_sha256"),
                    "completion_tokens": g.get("completion_tokens"),
                    "prompt_tokens": g.get("prompt_tokens"),
                    "prompt_tokens_processed": g.get("prompt_tokens_processed"),
                    "llm_wall_s": g.get("llm_wall_s"), "compile_s": r.get("compile_s"),
                    "cases_pass": sum(1 for c in cases if c["status"] == "pass"), "n_cases": r.get("n_cases"),
                    "case_wall_s": round(sum(c.get("wall_s", 0) for c in cases), 4)})
    tot = {"arm": arm, "split": split, "n": len(out), "classes": counts(out),
           "completion_tokens": sum(o["completion_tokens"] or 0 for o in out),
           "prompt_tokens_processed": sum(o["prompt_tokens_processed"] or 0 for o in out),
           "llm_wall_s": round(sum(o["llm_wall_s"] or 0 for o in out), 3),
           "cpu_core_s_modeled": round(CPU_CORES * sum(o["llm_wall_s"] or 0 for o in out), 3)}
    p = os.path.join(rundir, f"summary-{split}.json")
    with open(p, "w") as f:
        json.dump({"totals": tot, "tasks": out}, f, indent=1, sort_keys=True)
    print(p, json.dumps(tot, sort_keys=True))


def main():
    global COMMON_IO
    COMMON_IO = load_common_io()
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd")
    ap.add_argument("--arm")
    ap.add_argument("--split")
    ap.add_argument("--run")
    ap.add_argument("--tasks")
    ap.add_argument("--gate-log")
    a = ap.parse_args()
    if sha_file(OMC) != OMC_SHA:
        sys.exit("FATAL omc sha256 mismatch")
    if a.cmd == "gen":
        os.makedirs(a.run, exist_ok=True)
        _, tids = task_dirs(a.split)
        if a.tasks:
            want = a.tasks.split(",")
            tids = [t for t in tids if t in want]
        with open(os.path.join(a.run, "gen.log"), "a") as log:
            for tid in tids:
                gen_task(a.arm, load_task(a.split, tid), a.run, log)
    elif a.cmd == "judge":
        judge(a.run, a.split, a.arm)
    elif a.cmd == "gatelist":
        gatelist(a.run, a.split)
    elif a.cmd == "crosscheck":
        sys.exit(crosscheck(a.run, a.split, a.gate_log))
    elif a.cmd == "summary":
        summary(a.run, a.split, a.arm)
    else:
        sys.exit("unknown command")


if __name__ == "__main__":
    main()
