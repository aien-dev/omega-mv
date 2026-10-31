#!/usr/bin/env python3
"""TEST-ONLY scripted model server for the equal-work benchmark (omagent vs nanobot).

Usage: equal_mock.py PORTFILE DELAY_MS
Every POST /v1/chat/completions must ask for stream=true. The reply depends on how many tool
results follow the last user message, so both harnesses walk the same four-step script:
  0 -> call read_file   {path: notes.txt}                      result must contain alpha-123
  1 -> call write_file  {path: out.txt, content: beta-456}      result must mention 8
  2 -> call the shell   /bin/echo gamma-789                     result must contain gamma-789
  3 -> final answer "done delta-000"
The shell call uses each harness's own schema: omagent run_command {argv:[...]}, nanobot exec
{command: "..."}. Each reply is streamed as TOKENS chunks with DELAY_MS before every chunk, so
DELAY_MS=0 measures the harness and DELAY_MS=50 imitates a 50 ms/token model.
GET /state returns and resets {first_ns, last_ns, posts, bad, tools, req_bytes}; times are
CLOCK_MONOTONIC nanoseconds of request arrival, comparable with the driver's monotonic_ns."""
import http.server, json, socketserver, sys, threading, time

TOKENS = 10
DELAY = int(sys.argv[2]) / 1000.0
LOCK = threading.Lock()
STATE = {}


def reset():
    STATE.clear()
    STATE.update(first_ns=0, last_ns=0, posts=0, bad=[], tools=[], req_bytes=0)


reset()
EXPECT = {1: "alpha-123", 2: "8", 3: "gamma-789"}


def chunk(delta, finish=None):
    return {"id": "x", "object": "chat.completion.chunk", "created": 0, "model": "mock-1",
            "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}


def split(s, n):
    k = max(1, -(-len(s) // n))
    return [s[i:i + k] for i in range(0, len(s), k)] or [""]


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *a):
        pass

    def do_GET(self):
        with LOCK:
            b = json.dumps(STATE).encode()
            reset()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        now = time.monotonic_ns()
        raw = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        body = json.loads(raw or b"{}")
        msgs = body.get("messages", [])
        last_user = max([i for i, m in enumerate(msgs) if m.get("role") == "user"] or [0])
        tool_msgs = [m for m in msgs[last_user:] if m.get("role") == "tool"]
        step = len(tool_msgs)
        names = [t.get("function", {}).get("name") for t in body.get("tools", [])]
        with LOCK:
            STATE["first_ns"] = STATE["first_ns"] or now
            STATE["posts"] += 1
            STATE["req_bytes"] += len(raw)
            STATE["tools"] = sorted(n for n in names if n)
            if not body.get("stream"):
                STATE["bad"].append("step %d: stream not requested" % step)
            if step in EXPECT:
                c = tool_msgs[-1].get("content")
                c = c if isinstance(c, str) else json.dumps(c)
                if EXPECT[step] not in c:
                    STATE["bad"].append("step %d: tool result lacks %s: %r" % (step, EXPECT[step], c[:200]))
        om = "run_command" in names
        if step == 0:
            call = ("read_file", {"path": "notes.txt"})
        elif step == 1:
            call = ("write_file", {"path": "out.txt", "content": "beta-456"})
        elif step == 2:
            # nanobot's restrictToWorkspace guard refuses absolute paths outside the workspace, so
            # its command reaches the same echo through PATH; env makes it a real exec, not a builtin.
            call = ("run_command", {"argv": ["/bin/echo", "gamma-789"]}) if om else ("exec", {"command": "env echo gamma-789"})
        else:
            call = None
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        evs = []
        if call is None:
            for i, piece in enumerate(split("done delta-000", TOKENS)):
                evs.append(chunk({"role": "assistant", "content": piece} if i == 0 else {"content": piece}))
            evs.append(chunk({}, "stop"))
        else:
            args = split(json.dumps(call[1]), TOKENS)
            for i, piece in enumerate(args):
                tc = {"index": 0, "function": {"arguments": piece}}
                if i == 0:
                    tc.update(id="call_%d" % step, type="function")
                    tc["function"]["name"] = call[0]
                evs.append(chunk({"role": "assistant", "tool_calls": [tc]} if i == 0 else {"tool_calls": [tc]}))
            evs.append(chunk({}, "tool_calls"))
        for i, ev in enumerate(evs):
            if DELAY and i < TOKENS:
                time.sleep(DELAY)
            self.wfile.write(("data: %s\n\n" % json.dumps(ev)).encode())
            self.wfile.flush()
        self.wfile.write(b"data: [DONE]\n\n")


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


s = S(("127.0.0.1", 0), H)
assert s.server_address[1] != 18091
open(sys.argv[1], "w").write(str(s.server_address[1]))
s.serve_forever()
