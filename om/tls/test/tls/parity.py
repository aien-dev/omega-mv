#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test oracle only: fetch the same URL with the Omega client (client.om build) and with Python's
# ssl module (TLS 1.3, system trust store), each sending the identical HTTP/1.1 request bytes, and
# compare the decoded response bodies byte for byte (headers carry per-request values such as Date
# and rate-limit counters, so only status line and body are compared).
# usage: parity.py CLIENT HOST PATH...   prints one PARITY line per path; exit 0 only if all match.
#        parity.py CLIENT --smtp HOST PORT smtp|tsmtp   compares the SMTP dialogue (banner, EHLO
#        reply, QUIT reply) over implicit TLS (smtp) or STARTTLS (tsmtp) with the same via ssl.
import hashlib, socket, ssl, subprocess, sys

def request(host, path):
    return (f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUser-Agent: omega-tls/1\r\n"
            f"Accept: */*\r\nConnection: close\r\n\r\n").encode()

def py_fetch(host, path):
    ctx = ssl.create_default_context()
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    ctx.set_alpn_protocols(["http/1.1"])
    with socket.create_connection((host, 443), timeout=30) as raw:
        with ctx.wrap_socket(raw, server_hostname=host) as s:
            s.sendall(request(host, path))
            out = b""
            while True:
                b = s.recv(65536)
                if not b:
                    return out
                out += b

def split(resp):
    head, _, body = resp.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    hdr = {l.split(b":", 1)[0].strip().lower(): l.split(b":", 1)[1].strip() for l in lines[1:] if b":" in l}
    if hdr.get(b"transfer-encoding", b"").lower() == b"chunked":
        out, p = b"", 0
        while True:
            e = body.index(b"\r\n", p)
            n = int(body[p:e].split(b";")[0], 16)
            if n == 0:
                break
            out += body[e + 2:e + 2 + n]
            p = e + 2 + n + 2
        body = out
    return lines[0], body

def py_smtp(host, port, mode):
    ctx = ssl.create_default_context()
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    raw = socket.create_connection((host, port), timeout=30)
    f = raw.makefile("rb")
    out = b""
    def reply(fh):
        nonlocal out
        while True:
            l = fh.readline()
            out += l
            if len(l) < 4 or l[3:4] == b" ":
                return
    if mode == "tsmtp":
        reply(f); raw.sendall(b"EHLO omega-tls.invalid\r\n"); reply(f)
        raw.sendall(b"STARTTLS\r\n"); reply(f)
    s = ctx.wrap_socket(raw, server_hostname=host)
    g = s.makefile("rb")
    if mode == "smtp":
        reply(g)
    s.sendall(b"EHLO omega-tls.invalid\r\n"); reply(g)
    s.sendall(b"QUIT\r\n"); reply(g)
    s.close()
    return out

def smtp_main(client, host, port, mode):
    r = subprocess.run([client, host, port, mode], capture_output=True, timeout=60)
    o = r.stdout
    p = py_smtp(host, int(port), mode)
    ok = r.returncode == 0 and o == p
    print(f"PARITY {'PASS' if ok else 'FAIL'} {mode}://{host}:{port} exit={r.returncode} bytes={len(o)}/{len(p)} "
          f"sha256={hashlib.sha256(o).hexdigest()[:16]}/{hashlib.sha256(p).hexdigest()[:16]}")
    sys.exit(0 if ok else 1)

def main():
    if sys.argv[2] == "--smtp":
        smtp_main(sys.argv[1], sys.argv[3], sys.argv[4], sys.argv[5])
    client, host, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
    bad = 0
    for path in paths:
        r = subprocess.run([client, host, "443", "get", host, "-", path], capture_output=True, timeout=60)
        if r.returncode != 0:
            print(f"PARITY FAIL {host}{path}: omega client exit {r.returncode} {r.stderr.decode().strip()}")
            bad += 1
            continue
        s1, b1 = split(r.stdout)
        s2, b2 = split(py_fetch(host, path))
        h1, h2 = hashlib.sha256(b1).hexdigest(), hashlib.sha256(b2).hexdigest()
        ok = s1 == s2 and b1 == b2
        print(f"PARITY {'PASS' if ok else 'FAIL'} https://{host}{path} status={s1.decode()!r} "
              f"bytes={len(b1)}/{len(b2)} sha256={h1[:16]}/{h2[:16]}")
        bad += 0 if ok else 1
    sys.exit(1 if bad else 0)

main()
