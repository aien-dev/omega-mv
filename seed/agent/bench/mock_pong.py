#!/usr/bin/env python3
"""TEST-ONLY benchmark mock: every POST gets a streamed (or JSON, if stream is false) "pong" reply.
Writes its kernel-chosen 127.0.0.1 port to argv[1]. Logs request paths to argv[2]."""
import http.server, json, socketserver, sys
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    def log_message(self, *a): pass
    def do_GET(self):
        open(sys.argv[2], "a").write("GET %s\n" % self.path)
        b = b'{"data":[{"id":"mock-1","object":"model"}]}'
        self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0")); body = json.loads(self.rfile.read(n) or b"{}")
        open(sys.argv[2], "a").write("POST %s stream=%s\n" % (self.path, body.get("stream")))
        if body.get("stream"):
            self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.end_headers()
            for ev in [{"id":"x","object":"chat.completion.chunk","created":0,"model":"mock-1","choices":[{"index":0,"delta":{"role":"assistant","content":"pong"},"finish_reason":None}]},
                       {"id":"x","object":"chat.completion.chunk","created":0,"model":"mock-1","choices":[{"index":0,"delta":{},"finish_reason":"stop"}]}]:
                self.wfile.write(("data: %s\n\n" % json.dumps(ev)).encode())
            self.wfile.write(b"data: [DONE]\n\n")
        else:
            b = json.dumps({"id":"x","object":"chat.completion","created":0,"model":"mock-1","choices":[{"index":0,"message":{"role":"assistant","content":"pong"},"finish_reason":"stop"}],"usage":{"prompt_tokens":1,"completion_tokens":1,"total_tokens":2}}).encode()
            self.send_response(200); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
class S(socketserver.ThreadingMixIn, http.server.HTTPServer): daemon_threads = True
s = S(("127.0.0.1", 0), H); open(sys.argv[1], "w").write(str(s.server_address[1])); s.serve_forever()
