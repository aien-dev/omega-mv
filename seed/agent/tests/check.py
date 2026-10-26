#!/usr/bin/env python3
"""seed/agent/tests/check.py -- host check of seed/agent/agent.om (omagent).

TEST-ONLY. Python is only the referee: it plays a scripted OpenAI-compatible model server
(chat completions, SSE and plain JSON) on a kernel-chosen 127.0.0.1 port, runs the compiled
Omega binary against it, and compares exit codes, stdout, stderr, the requests the server saw,
the session file and the workspace. Every implementation byte under test is compiled .om.

Usage:  nice -n 19 ionice -c3 python3 seed/agent/tests/check.py [--omc PATH] [--bin PATH] [--keep DIR]
Stages: build, cases (one scripted conversation each), controls (a wrong expectation must
fail; source mutants of agent.om must each be caught by the cases).
Exit 0 = PASS, 1 = FAIL, 3 = NOT_RUN (the compiler is missing).
Never touches port 18091; never reaches outside 127.0.0.1.
"""
import http.server
import json
import os
import shutil
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
AGENT = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(AGENT))
SOURCES = ["seed/json/json.om", "seed/json/jsonw.om", "seed/net/http.om", "seed/agent/agent.om"]
KEY = "sk-omagent-test-7f3a9c41d2e8b605"
LOG = []


def log(s):
    print(s, flush=True)


# ---------------------------------------------------------------- scripted model server

class Mock:
    """One response script per request: entries are consumed in order."""

    def __init__(self):
        self.script = []
        self.requests = []
        self.lock = threading.Lock()


MOCK = Mock()


def sse_event(obj):
    if isinstance(obj, str):
        return ("data: %s\n\n" % obj).encode()
    return ("data: %s\n\n" % json.dumps(obj)).encode()


def delta_text(t):
    return {"choices": [{"index": 0, "delta": {"content": t}}]}


def delta_call(i, cid, name, args):
    fn = {}
    if name is not None:
        fn["name"] = name
    if args is not None:
        fn["arguments"] = args
    tc = {"index": i, "function": fn}
    if cid is not None:
        tc["id"] = cid
        tc["type"] = "function"
    return {"choices": [{"index": 0, "delta": {"tool_calls": [tc]}}]}


def finish(reason):
    return {"choices": [{"index": 0, "delta": {}, "finish_reason": reason}]}


def sse_text(*parts):
    ev = [delta_text(p) for p in parts] + [finish("stop"), "[DONE]"]
    return ("sse", ev)


def sse_calls(calls):
    """calls: list of (name, arguments-json-string); arguments are split in two deltas."""
    ev = []
    for i, (name, args) in enumerate(calls):
        h = len(args) // 2
        ev.append(delta_call(i, "c%d" % i, name, args[:h]))
        ev.append(delta_call(i, None, None, args[h:]))
    ev += [finish("tool_calls"), "[DONE]"]
    return ("sse", ev)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *a):
        pass

    def do_GET(self):
        with MOCK.lock:
            MOCK.requests.append({"method": "GET", "path": self.path, "headers": dict(self.headers), "body": None})
        body = b"hello from the fetch page\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(n)
        try:
            body = json.loads(raw)
        except Exception:
            body = {"_unparsed": raw.decode("latin-1")}
        with MOCK.lock:
            MOCK.requests.append({"method": "POST", "path": self.path, "headers": dict(self.headers), "body": body})
            step = MOCK.script.pop(0) if MOCK.script else ("status", 500, "script exhausted")
        kind = step[0]
        if kind == "sse":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for ev in step[1]:
                self.wfile.write(sse_event(ev))
                self.wfile.flush()
        elif kind == "rawsse":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            self.wfile.write(step[1])
        elif kind == "json":
            b = json.dumps(step[1]).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
        elif kind == "status":
            b = step[2].encode()
            self.send_response(step[1])
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
        elif kind == "stall":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            self.wfile.write(sse_event(delta_text("partial ")))
            self.wfile.flush()
            time.sleep(step[1])


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


# ---------------------------------------------------------------- cases

def run(binp, port, work, args, stdin=None, env_key=True, timeout=30):
    env = {"PATH": "/usr/bin:/bin"}
    if env_key:
        env["OMAGENT_API_KEY"] = KEY
    argv = [binp, "--url", "http://127.0.0.1:%d/v1/chat/completions" % port, "--model", "mock-1",
            "--workdir", work] + [os.path.join(work, x) if x == "s.jsonl" else x for x in args]
    t0 = time.monotonic()
    p = subprocess.run(argv, input=stdin, capture_output=True, env=env, timeout=timeout)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), time.monotonic() - t0


def tool_msgs(req):
    return [m for m in req["body"].get("messages", []) if m.get("role") == "tool"]


def C(name, script, args, expect_rc, checks, stdin=None, prep=None, env_key=True, timeout=30):
    return {"name": name, "script": script, "args": args, "rc": expect_rc, "checks": checks,
            "stdin": stdin, "prep": prep, "env_key": env_key, "timeout": timeout}


def has(s):
    return lambda o: s in o["out"]


def err_has(s):
    return lambda o: s in o["err"]


def last_tool_has(s):
    def f(o):
        posts = [r for r in o["reqs"] if r["method"] == "POST"]
        t = tool_msgs(posts[-1]) if posts else []
        return bool(t) and s in t[-1]["content"]
    return f


def nposts(n):
    return lambda o: len([r for r in o["reqs"] if r["method"] == "POST"]) == n


def file_is(rel, content):
    def f(o):
        p = os.path.join(o["work"], rel)
        return os.path.isfile(p) and open(p, "rb").read() == content.encode()
    return f


def no_file(rel):
    return lambda o: not os.path.exists(os.path.join(o["work"], rel))


def no_key_leak(o):
    blob = o["out"] + o["err"]
    sess = os.path.join(o["work"], "s.jsonl")
    if os.path.exists(sess):
        blob += open(sess, encoding="utf-8", errors="replace").read()
    return KEY not in blob


def bearer_sent(o):
    posts = [r for r in o["reqs"] if r["method"] == "POST"]
    return bool(posts) and all(r["headers"].get("Authorization") == "Bearer " + KEY for r in posts)


def request_shape(o):
    r = [r for r in o["reqs"] if r["method"] == "POST"][0]
    b = r["body"]
    names = [t["function"]["name"] for t in b.get("tools", [])]
    return (r["path"] == "/v1/chat/completions" and b.get("model") == "mock-1" and b.get("stream") is True
            and names == ["read_file", "write_file", "run_command", "http_fetch"]
            and b["messages"][0]["role"] == "system" and b["messages"][-1] == {"role": "user", "content": "hi"})


def session_lines(n):
    def f(o):
        p = os.path.join(o["work"], "s.jsonl")
        if not os.path.exists(p):
            return False
        ls = open(p, encoding="utf-8").read().splitlines()
        return len(ls) == n and all(isinstance(json.loads(x), dict) for x in ls)
    return f


def prep_file(rel, content):
    def f(work):
        with open(os.path.join(work, rel), "w") as fh:
            fh.write(content)
    return f


def prep_symlink(work):
    os.symlink("/etc", os.path.join(work, "lnk"))


def prep_bad_session(work):
    with open(os.path.join(work, "s.jsonl"), "w") as fh:
        fh.write('{"role":"user","content":"a"}\nnot json\n')


def prep_good_session(work):
    with open(os.path.join(work, "s.jsonl"), "w") as fh:
        fh.write('{"role":"user","content":"earlier question"}\n{"role":"assistant","content":"earlier answer"}\n')


def resent_history(o):
    b = [r for r in o["reqs"] if r["method"] == "POST"][0]["body"]
    ms = b["messages"]
    return (ms[1] == {"role": "user", "content": "earlier question"}
            and ms[2] == {"role": "assistant", "content": "earlier answer"}
            and ms[3] == {"role": "user", "content": "hi"})


def cases(port):
    fetch = "127.0.0.1:%d" % port
    S = ["-p", "hi", "--session", "s.jsonl"]
    return [
        C("plain-sse", [sse_text("Hello", ", world")], S, 0,
          [has("Hello, world"), nposts(1), request_shape, bearer_sent, no_key_leak, session_lines(2)]),
        C("plain-json", [("json", {"choices": [{"index": 0, "message": {"role": "assistant", "content": "json reply"},
                                                 "finish_reason": "stop"}]})], S, 0, [has("json reply"), nposts(1)]),
        C("read-file", [sse_calls([("read_file", '{"path":"notes.txt"}')]), sse_text("done")], S, 0,
          [has("done"), nposts(2), last_tool_has("secret plan 42"), err_has("tool read_file: ok"), session_lines(4)],
          prep=prep_file("notes.txt", "secret plan 42\n")),
        C("write-file", [sse_calls([("write_file", '{"path":"out/x.txt","content":"written by agent"}')]), sse_text("ok")],
          S + ["--allow-write"], 0, [file_is("out/x.txt", "written by agent"), last_tool_has("ok: wrote 16 bytes")],
          prep=lambda w: os.mkdir(os.path.join(w, "out"))),
        C("run-echo", [sse_calls([("run_command", '{"argv":["/bin/echo","hi","there"]}')]), sse_text("ran")],
          S + ["--allow-cmd", "/bin/echo"], 0, [last_tool_has("hi there\n"), last_tool_has("[exit status 0]")]),
        C("run-exit-code", [sse_calls([("run_command", '{"argv":["/bin/false"]}')]), sse_text("ran")],
          S + ["--allow-cmd", "/bin/false"], 0, [last_tool_has("[exit status 1]")]),
        C("fetch", [sse_calls([("http_fetch", '{"url":"http://%s/page"}' % fetch)]), sse_text("fetched")],
          S + ["--allow-fetch", fetch], 0, [last_tool_has("status=200\nhello from the fetch page")]),
        C("two-calls-one-turn", [sse_calls([("read_file", '{"path":"a"}'), ("read_file", '{"path":"b"}')]), sse_text("both")],
          S, 0, [lambda o: [m["content"] for m in tool_msgs([r for r in o["reqs"] if r["method"] == "POST"][-1])] == ["A", "B"],
                 lambda o: [m["tool_call_id"] for m in tool_msgs([r for r in o["reqs"] if r["method"] == "POST"][-1])] == ["c0", "c1"]],
          prep=lambda w: (prep_file("a", "A")(w), prep_file("b", "B")(w))),
        # refusals: the tool result says refused, the agent continues, nothing is executed
        C("refuse-cmd-not-allowed", [sse_calls([("run_command", '{"argv":["/bin/rm","-rf","x"]}')]), sse_text("ok")],
          S + ["--allow-cmd", "/bin/echo"], 0, [last_tool_has("refused: the program is not on the --allow-cmd list: /bin/rm"),
                                                err_has("tool run_command: refused")]),
        C("refuse-cmd-disabled", [sse_calls([("run_command", '{"argv":["/bin/echo","x"]}')]), sse_text("ok")],
          S, 0, [last_tool_has("refused: run_command is disabled")]),
        C("refuse-dotdot", [sse_calls([("read_file", '{"path":"../etc/passwd"}')]), sse_text("ok")],
          S, 0, [last_tool_has("refused: the path must be relative")]),
        C("refuse-absolute", [sse_calls([("read_file", '{"path":"/etc/passwd"}')]), sse_text("ok")],
          S, 0, [last_tool_has("refused: the path must be relative")]),
        C("refuse-symlink", [sse_calls([("read_file", '{"path":"lnk/passwd"}')]), sse_text("ok")],
          S, 0, [last_tool_has("refused: the path must be relative")], prep=prep_symlink),
        C("refuse-write-disabled", [sse_calls([("write_file", '{"path":"y.txt","content":"x"}')]), sse_text("ok")],
          S, 0, [last_tool_has("refused: write_file is disabled"), no_file("y.txt")]),
        C("refuse-unknown-tool", [sse_calls([("format_disk", '{}')]), sse_text("ok")],
          S, 0, [last_tool_has("refused: unknown tool format_disk")]),
        C("refuse-fetch-host", [sse_calls([("http_fetch", '{"url":"http://10.1.2.3:80/"}')]), sse_text("ok")],
          S + ["--allow-fetch", fetch], 0, [last_tool_has("refused: the host:port is not on the --allow-fetch list: 10.1.2.3:80")]),
        C("refuse-https-fetch", [sse_calls([("http_fetch", '{"url":"https://%s/page"}' % fetch)]), sse_text("ok")],
          S + ["--allow-fetch", fetch], 0, [last_tool_has("refused: only http:// URLs")]),
        C("bad-tool-args", [sse_calls([("read_file", '[1,2]')]), sse_text("ok")],
          S, 0, [last_tool_has("error: tool arguments are not a JSON object")]),
        # malformed model output
        C("malformed-event", [("rawsse", b"data: {not json\n\n")], S, 6, [err_has("malformed model output")]),
        C("truncated-stream", [("rawsse", b'data: {"choices":[{"index":0,"delta":{"content":"cut"}}]}\n\n')], S, 6,
          [err_has("the stream ended before [DONE]")]),
        C("server-error-object", [("sse", [{"error": {"message": "boom"}}])], S, 6, [err_has("error object")]),
        C("too-many-calls", [("sse", [delta_call(9, "c9", "read_file", "{}"), finish("tool_calls"), "[DONE]"])], S, 6,
          [err_has("more than 8 tool calls")]),
        C("http-500", [("status", 500, "internal oops")], S, 4, [err_has("status"), err_has("internal oops")]),
        # timeouts
        C("model-timeout", [("stall", 6)], S + ["--timeout", "2"], 5, [err_has("timed out"), lambda o: o["dt"] < 5.5]),
        C("cmd-timeout", [sse_calls([("run_command", '{"argv":["/bin/sleep","30"]}')]), sse_text("ok")],
          S + ["--allow-cmd", "/bin/sleep", "--cmd-timeout", "1"], 0,
          [last_tool_has("[killed: timeout after 1 s]"), lambda o: o["dt"] < 10]),
        C("round-limit", [sse_calls([("read_file", '{"path":"a"}')])] * 3, S + ["--max-rounds", "2"], 7,
          [err_has("round limit"), nposts(2)], prep=prep_file("a", "A")),
        # sessions and configuration
        C("session-reload", [sse_text("again")], S, 0, [resent_history, session_lines(4)], prep=prep_good_session),
        C("session-bad-line", [sse_text("never")], S, 8, [nposts(0), err_has("session file line")], prep=prep_bad_session),
        C("https-model-refused", [], ["-p", "hi", "--url", "https://127.0.0.1:1/x"], 2, [nposts(0)]),
        C("no-key-no-header", [sse_text("anon")], S, 0,
          [lambda o: "Authorization" not in [r for r in o["reqs"] if r["method"] == "POST"][0]["headers"]], env_key=False),
        C("repl-two-lines", [sse_text("one"), sse_text("two")], ["--session", "s.jsonl"], 0,
          [has("one"), has("two"), nposts(2), session_lines(4)], stdin=b"first\nsecond\n"),
        C("connect-refused", [], ["-p", "hi", "--url", "http://127.0.0.1:1/v1"], 3, [err_has("cannot connect")]),
    ]


def run_case(binp, port, base, c):
    work = tempfile.mkdtemp(prefix=c["name"] + "-", dir=base)
    if c["prep"]:
        c["prep"](work)
    with MOCK.lock:
        MOCK.script = list(c["script"])
        MOCK.requests = []
    rc, out, err, dt = run(binp, port, work, c["args"], c["stdin"], c["env_key"], c["timeout"])
    o = {"out": out, "err": err, "work": work, "reqs": list(MOCK.requests), "dt": dt}
    fails = []
    if rc != c["rc"]:
        fails.append("rc=%d want %d" % (rc, c["rc"]))
    for i, chk in enumerate(c["checks"]):
        try:
            ok = chk(o)
        except Exception as e:
            ok = False
        if not ok:
            fails.append("check %d" % i)
    if c["name"] != "no-key-no-header" and not no_key_leak(o):
        fails.append("key leaked")
    return fails, o, rc, dt


def build(omc, out):
    src = b"om2\n" + b"".join(open(os.path.join(ROOT, s), "rb").read() for s in SOURCES)
    p = subprocess.run([omc], input=src, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError("build failed: " + p.stderr.decode()[:400])
    with open(out, "wb") as fh:
        fh.write(p.stdout)
    os.chmod(out, 0o755)


# source mutants: each weakens one safety or protocol rule; the cases must catch every one
MUTANTS = [
    ("allowlist-bypass", "if eq k 0\nif or eq m 0 not agallowed ICMD get gnum QNCMD view part e1 0 m", "if eq k 1\nif or eq m 0 not agallowed ICMD get gnum QNCMD view part e1 0 m"),
    ("dotdot-allowed", "if eq cl 2\nif eq get p add s 1 46\nret false", "if eq cl 2\nif eq get p add s 1 47\nret false"),
    ("symlink-followed", "ret ne and md 61440 40960", "ret true"),
    ("fetch-allowlist-bypass", "if not agallowed IFET nf view part cn 0 cl", "if eq nf 999"),
    ("write-flag-ignored", "if eq get gnum QWRITE 0", "if eq get gnum QWRITE 7"),
    ("no-done-accepted", "if eq get gst SFIN 0\nagerr enodone\nret 6", "if eq get gst SFIN 7\nagerr enodone\nret 6"),
]


def run_mutants(omc, port, base):
    src0 = open(os.path.join(AGENT, "agent.om")).read()
    killed = 0
    for name, a, b in MUTANTS:
        if src0.count(a) != 1:
            raise RuntimeError("mutant %s: anchor found %d times" % (name, src0.count(a)))
        src = b"om2\n" + b"".join(open(os.path.join(ROOT, s), "rb").read() for s in SOURCES[:3]) + src0.replace(a, b).encode()
        p = subprocess.run([omc], input=src, capture_output=True)
        if p.returncode != 0:
            raise RuntimeError("mutant %s does not compile" % name)
        mb = os.path.join(base, "mut-" + name)
        with open(mb, "wb") as fh:
            fh.write(p.stdout)
        os.chmod(mb, 0o755)
        caught = []
        for c in cases(port):
            f, _, _, _ = run_case(mb, port, base, c)
            if f:
                caught.append(c["name"])
        if caught:
            killed += 1
            log("AGENT-MUTANT %s KILLED by %s" % (name, ",".join(caught[:3])))
        else:
            log("AGENT-MUTANT %s SURVIVED" % name)
    return killed


def main():
    a = sys.argv[1:]
    omc = os.path.join(ROOT, "seed/out/omc")
    binp = None
    keep = None
    while a:
        k = a.pop(0)
        if k == "--omc":
            omc = a.pop(0)
        elif k == "--bin":
            binp = a.pop(0)
        elif k == "--keep":
            keep = a.pop(0)
    if not os.access(omc, os.X_OK):
        log("AGENT-CHECK NOT_RUN no compiler at %s" % omc)
        return 3
    base = keep or tempfile.mkdtemp(prefix="omagent-check-")
    os.makedirs(base, exist_ok=True)
    srv = Server(("127.0.0.1", 0), Handler)
    port = srv.server_address[1]
    assert port != 18091
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    try:
        if binp is None:
            binp = os.path.join(base, "omagent")
            build(omc, binp)
            log("AGENT-BUILD PASS bytes=%d" % os.path.getsize(binp))
        bad = 0
        cs = cases(port)
        for c in cs:
            f, o, rc, dt = run_case(binp, port, base, c)
            if f:
                bad += 1
                log("AGENT-CASE %s FAIL %s rc=%d dt=%.2f\n  stdout=%r\n  stderr=%r" % (c["name"], "; ".join(f), rc, dt, o["out"][:300], o["err"][:300]))
            else:
                log("AGENT-CASE %s PASS rc=%d dt=%.2f" % (c["name"], rc, dt))
        if bad:
            log("AGENT-CHECK FAIL failed=%d/%d" % (bad, len(cs)))
            return 1
        # negative control: a deliberately wrong expectation must fail
        wrong = dict(cs[0])
        wrong["rc"] = 6
        f, _, _, _ = run_case(binp, port, base, wrong)
        if not f:
            log("AGENT-CONTROL FAIL a wrong expectation passed")
            return 1
        wrong = dict(cs[0])
        wrong["checks"] = [has("text the model never sent")]
        f, _, _, _ = run_case(binp, port, base, wrong)
        if not f:
            log("AGENT-CONTROL FAIL a wrong output expectation passed")
            return 1
        log("AGENT-CONTROL PASS wrong expectations fail")
        k = run_mutants(omc, port, base)
        if k != len(MUTANTS):
            log("AGENT-MUTANTS FAIL killed=%d/%d" % (k, len(MUTANTS)))
            return 1
        log("AGENT-MUTANTS PASS killed=%d/%d" % (k, len(MUTANTS)))
        log("AGENT-CHECK PASS cases=%d mutants=%d" % (len(cs), len(MUTANTS)))
        return 0
    except Exception as e:
        log("AGENT-CHECK FAIL %s" % e)
        return 1
    finally:
        srv.shutdown()
        if keep is None:
            shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
