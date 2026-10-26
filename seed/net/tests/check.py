#!/usr/bin/env python3
"""seed/net/tests/check.py -- host check of seed/net/http.om (omega-mv #788).

Python is only the referee here: it builds the corpus, computes expected results with an
independent reference parser (written from RFC 9112, not from http.om), cross-checks that
reference against the standard library's http.client, drives curl and http.client against the
Omega server, and compares bytes. Every implementation byte under test is compiled .om.

Usage:  nice -n 19 ionice -c3 python3 seed/net/tests/check.py [--omc PATH] [--keep DIR]
Stages: build, corpus (hand cases + deterministic fuzz, seed 788), mutants (each must be
caught), live (tests/live.om: keep-alive, 300 KiB / 1 MiB bodies, chunked, HEAD, pipelining,
SSE, malformed requests, deadlines, fd leaks), e2e (examples/sse_server.om under curl and
http.client, examples/sse_client.om, server fd count).
Exit 0 = PASS, 1 = FAIL, 3 = NOT_RUN (a prerequisite such as curl or the compiler is missing).
Sockets: 127.0.0.1 with kernel-chosen ephemeral ports only. Never touches port 18091.
"""
import hashlib
import http.client
import io
import os
import random
import shutil
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
NET = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(NET))
LIB = os.path.join(NET, "http.om")

HEADMX = 2048      # parse.om: put cs HHEADMX 2048
HHMAX = 16         # parse.om: put cs HHMAX 16
BCAP = 4096        # parse.om: put cs HBCAP 4096
FRMAX = 1024       # parse.om: put cs HFRMAX 1024
FNV_OFF = 14695981039346656037
FNV_PRIME = 1099511628211
M64 = (1 << 64) - 1
TCHAR = set(b"!#$%&'*+-.^_`|~0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
FUZZ_SEED = 788
FUZZ_COUNT = 1500

if shutil.which("ionice"):
    NICE = ["nice", "-n", "19", "ionice", "-c3"]
else:
    NICE = ["nice", "-n", "19"]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def fnv(h, data):
    for c in data:
        h = ((h ^ c) * FNV_PRIME) & M64
    return h


# ======== reference parser (RFC 9112 with the documented http.om caps and choices)

class Err(Exception):
    def __init__(self, code):
        Exception.__init__(self, code)
        self.code = code


def low(c):
    return c + 32 if 65 <= c <= 90 else c


def ieq(a, t):
    return len(a) == len(t) and all(low(x) == low(y) for x, y in zip(a, t))


def vch(c):
    return c == 9 or (c >= 32 and c != 127)


def scan_head(buf, server):
    """Index after CRLF CRLF, or None for a clean end of input; raises Err."""
    n = len(buf)
    st = 0
    if server:
        while st + 2 <= n and buf[st] == 13 and buf[st + 1] == 10:
            st += 2
    lim = min(n, HEADMX)
    i = st
    while i < lim:
        c = buf[i]
        if c == 10:
            if i == st or buf[i - 1] != 13:
                raise Err(400)
            if i >= st + 3 and buf[i - 2] == 10 and buf[i - 3] == 13:
                return st, i + 1
        if c == 13:
            if i + 1 >= n:
                break
            if buf[i + 1] != 10:
                raise Err(400)
        i += 1
    if n >= HEADMX:
        raise Err(431)
    if n == st:
        if server:
            return st, None
        raise Err(499)
    raise Err(499)


def version(b):
    if b[:5] != b"HTTP/":
        raise Err(400)
    mj, dt, mn = b[5], b[6], b[7]
    if not (48 <= mj <= 57) or dt != 46 or not (48 <= mn <= 57):
        raise Err(400)
    if mj != 49 or mn not in (48, 49):
        raise Err(505)
    return mn - 48


def scheme_ok(t):
    if len(t) < 4 or not (97 <= low(t[0]) <= 122):
        return False
    for i in range(1, len(t)):
        c = low(t[i])
        if c == 58:
            return i + 3 <= len(t) and t[i + 1] == 47 and t[i + 2] == 47
        if not (97 <= c <= 122 or 48 <= c <= 57 or c in (43, 45, 46)):
            return False
    return False


def request_line(line, st):
    i = 0
    while i < len(line) and line[i] in TCHAR:
        i += 1
    if i == 0 or i > 32 or i >= len(line) or line[i] != 32:
        raise Err(400)
    method = line[:i]
    j = i + 1
    while j < len(line) and 33 <= line[j] <= 126:
        j += 1
    target = line[i + 1:j]
    if len(target) == 0 or len(target) > 8192 or j >= len(line) or line[j] != 32:
        raise Err(400)
    rest = line[j + 1:]
    if len(rest) != 8:
        raise Err(400)
    v = version(rest)
    if not (target[0] == 47 or target == b"*" or method == b"CONNECT" or scheme_ok(target)):
        raise Err(400)
    return method, target, v


def status_line(line):
    if len(line) < 8:
        raise Err(400)
    v = version(line[:8])
    if len(line) < 13 or line[8] != 32 or line[12] != 32:
        raise Err(400)
    d = line[9:12]
    if not all(48 <= c <= 57 for c in d):
        raise Err(400)
    s = int(d)
    if s < 100 or s > 599:
        raise Err(400)
    if not all(vch(c) for c in line[13:]):
        raise Err(400)
    return s, v


def field_line(a):
    if len(a) == 0 or a[0] in (32, 9):
        raise Err(400)
    c = 0
    while c < len(a) and a[c] in TCHAR:
        c += 1
    if c == 0 or c == len(a) or a[c] != 58:
        raise Err(400)
    if not all(vch(x) for x in a[c + 1:]):
        raise Err(400)
    return a[:c], a[c + 1:].strip(b" \t")


def parse_head(buf, server, ishead):
    st, e = scan_head(buf, server)
    if e is None:
        return None
    lines = buf[st:e - 4].split(b"\r\n")
    m = {"st": st, "end": e, "status": 0, "fields": []}
    if server:
        m["method"], m["target"], m["ver"] = request_line(lines[0], st)
    else:
        m["status"], m["ver"] = status_line(lines[0])
    for ln in lines[1:]:
        name, val = field_line(ln)
        if len(m["fields"]) >= HHMAX:
            raise Err(431)
        m["fields"].append((name, val))
    semantics(m, server, ishead)
    return m


def semantics(m, server, ishead):
    cl = te = None
    close = False
    hosts = 0
    for name, val in m["fields"]:
        if ieq(name, b"content-length"):
            if cl is not None:
                raise Err(400)
            if not (1 <= len(val) <= 19 and all(48 <= c <= 57 for c in val)):
                raise Err(400)
            cl = int(val)
        elif ieq(name, b"transfer-encoding"):
            if te is not None:
                raise Err(400)
            te = True
            if not ieq(val, b"chunked"):
                raise Err(501)
        elif ieq(name, b"connection"):
            for tok in val.split(b","):
                if ieq(tok.strip(b" \t"), b"close"):
                    close = True
        elif ieq(name, b"host"):
            hosts += 1
        elif ieq(name, b"expect"):
            if server and m["ver"] == 1 and not ieq(val, b"100-continue"):
                raise Err(417)
    if cl is not None and te:
        raise Err(400)
    if te and m["ver"] == 0:
        raise Err(400)
    if server and m["ver"] == 1 and hosts != 1:
        raise Err(400)
    m["keep"] = 1 if (m["ver"] == 1 and not close) else 0
    m["mode"], m["rem"] = 0, 0
    s = m["status"]
    if not server:
        if s == 101:
            m["keep"] = 0
        if ishead or s < 200 or s in (204, 304):
            return
    if te:
        m["mode"] = 2
    elif cl is not None:
        if cl > BCAP:
            raise Err(413)
        if cl > 0:
            m["mode"], m["rem"] = 1, cl
    elif not server:
        m["mode"], m["keep"] = 3, 0


def body_line(b, pos):
    """CR index of the CRLF line at pos (http.om hbline in memory mode)."""
    n = len(b)
    lim = min(n, pos + HEADMX)
    i = pos
    while i < lim:
        c = b[i]
        if c == 10:
            raise Err(400)
        if c == 13:
            if i + 1 >= n:
                break
            if b[i + 1] != 10:
                raise Err(400)
            return i
        i += 1
    if n - pos >= HEADMX:
        raise Err(431)
    raise Err(499)


def chunk_size(a):
    v, c = 0, len(a)
    for i, x in enumerate(a):
        h = int(chr(x), 16) if chr(x) in "0123456789abcdefABCDEF" else 16
        if h == 16:
            c = i
            break
        if i >= 15:
            raise Err(400)
        v = v * 16 + h
    if c == 0:
        raise Err(400)
    if c < len(a) and (a[c] != 59 or not all(vch(x) for x in a[c:])):
        raise Err(400)
    return v


def body(m, rest):
    mode = m["mode"]
    if mode == 0:
        return b""
    if mode == 1:
        if len(rest) < m["rem"]:
            raise Err(499)
        return rest[:m["rem"]]
    if mode == 3:
        if len(rest) > BCAP:
            raise Err(413)
        return rest
    out, pos, trn, frm = bytearray(), 0, 0, 0

    def framing(k):
        # http.om hfrchk: chunk framing bytes over HFRMAX + 8 * decoded bytes is 413
        nonlocal frm
        frm += k
        if frm > FRMAX + 8 * len(out):
            raise Err(413)

    while True:
        x = body_line(rest, pos)
        v = chunk_size(rest[pos:x])
        framing(x + 2 - pos)
        pos = x + 2
        if v == 0:
            break
        if len(out) + v > BCAP:
            raise Err(413)
        if len(rest) - pos < v:
            raise Err(499)
        out += rest[pos:pos + v]
        pos += v
        if len(rest) - pos < 2:
            raise Err(499)
        if rest[pos:pos + 2] != b"\r\n":
            raise Err(400)
        framing(2)
        pos += 2
    while True:
        x = body_line(rest, pos)
        framing(x + 2 - pos)
        if x == pos:
            return bytes(out)
        field_line(rest[pos:x])
        trn += 1
        if trn > HHMAX:
            raise Err(431)
        pos = x + 2


def reference(mode, msg):
    """The expected parse.om output (without the index) and the interim statuses seen."""
    server = mode == "q"
    interim = []
    try:
        buf = msg
        if server:
            m = parse_head(buf, True, False)
            if m is None:
                return "eof", interim
        else:
            for _ in range(16):
                m = parse_head(buf, False, mode == "h")
                if m["status"] >= 200 or m["status"] == 101:
                    break
                interim.append(m["status"])
                buf = buf[m["end"]:]
            else:
                raise Err(400)
        bd = body(m, buf[m["end"]:])
    except Err as e:
        return "err %d" % e.code, interim
    h = FNV_OFF
    if server:
        h = fnv(h, m["method"] + b" " + m["target"] + b"\n")
    for name, val in m["fields"]:
        h = fnv(h, bytes(low(c) for c in name) + b":" + val + b"\n")
    line = "ok %d %d %d %d %d %016x %d %016x" % (
        m["status"], m["ver"], m["keep"], m["mode"], len(m["fields"]), h, len(bd), fnv(FNV_OFF, bd))
    return line, interim


# ======== independent cross-check with the standard library client

class _Sock:
    def __init__(self, data):
        self.f = io.BytesIO(data)

    def makefile(self, *a, **k):
        return self.f


def stdlib_response(mode, msg):
    r = http.client.HTTPResponse(_Sock(msg), method="HEAD" if mode == "h" else "GET")
    try:
        r.begin()
        bd = r.read()
    except (http.client.HTTPException, ValueError):
        return None
    hdrs = [(k.lower().encode("latin-1"), v.encode("latin-1").strip(b" \t")) for k, v in r.getheaders()]
    return r.status, 1 if r.version == 11 else 0, hdrs, bd


PARITY = {"agree": 0, "both_reject": 0, "stricter": 0, "skipped": 0}


def cross_check(mode, msg, line, interim):
    """Problems where the reference disagrees with http.client (responses only)."""
    if mode == "q":
        return None
    if any(s != 100 for s in interim):
        PARITY["skipped"] += 1
        return None
    # http.client reads a chunked body even on 101/204/304 responses, which RFC 9112 6.3 gives
    # no body; those messages are outside the comparison.
    if line.startswith("ok") and line.split()[1] in ("101", "204", "304") and b"transfer-encoding" in msg.lower():
        PARITY["skipped"] += 1
        return None
    got = stdlib_response(mode, msg)
    if got is None:
        if line.startswith("ok"):
            return "http.client rejects a response the reference accepts"
        PARITY["both_reject"] += 1
        return None
    if not line.startswith("ok"):
        PARITY["stricter"] += 1
        return None
    PARITY["agree"] += 1
    status, ver, hdrs, bd = got
    p = line.split()
    if int(p[1]) != status or int(p[2]) != ver:
        return "status/version differ from http.client"
    if int(p[7]) != len(bd) or p[8] != "%016x" % fnv(FNV_OFF, bd):
        return "body differs from http.client"
    h = FNV_OFF
    for name, val in hdrs:
        h = fnv(h, name + b":" + val + b"\n")
    if p[6] != "%016x" % h or int(p[5]) != len(hdrs):
        return "fields differ from http.client"
    return None


# ======== corpus

def crlf(s):
    return s.replace(b"\n", b"\r\n")


def hand_cases():
    """(mode, message, expected outcome prefix): pins the reference itself."""
    q, s, h = "q", "s", "h"
    big16 = b"".join(b"X%d: v\n" % i for i in range(15))
    big17 = b"".join(b"X%d: v\n" % i for i in range(16))
    long_head = b"GET / HTTP/1.1\nHost: a\nX: " + b"a" * 2100 + b"\n\n"
    c4097 = b"1000\n" + b"a" * 4096 + b"\n1\nb\n0\n\n"
    extbomb = (b"1;" + b"a" * 1000 + b"\nX\n") * 3 + b"0\n\n"
    onebyte = b"1\nX\n" * 300 + b"0\n\n"
    trbomb = b"0\n" + (b"X-T: " + b"t" * 400 + b"\n") * 3 + b"\n"
    cases = [
        (q, crlf(b"GET / HTTP/1.1\nHost: a\n\n"), "ok 0 1 1 0 1"),
        (q, b"GET / HTTP/1.1\nHost: a\n\n", "err 400"),
        (q, b"GET / HTTP/1.1\r\nHost: a\n\r\n", "err 400"),
        (q, crlf(b"\n\nGET / HTTP/1.1\nHost: a\n\n"), "ok"),
        (q, b"", "eof"),
        (q, b"\r\n\r\n", "eof"),
        (q, crlf(b"GET  / HTTP/1.1\nHost: a\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1 \nHost: a\n\n"), "err 400"),
        (q, crlf(b"get / HTTP/1.1\nHost: a\n\n"), "ok"),
        (q, crlf(b"GET / http/1.1\nHost: a\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.0\n\n"), "ok 0 0 0 0 0"),
        (q, crlf(b"GET / HTTP/2.0\nHost: a\n\n"), "err 505"),
        (q, crlf(b"GET / HTTP/1.2\nHost: a\n\n"), "err 505"),
        (q, crlf(b"GET / HTTP/1.x\nHost: a\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nHost: b\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nX: 1\n folded\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nX: 1\n\tfolded\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\nHost : a\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\n Host: a\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 3\n\nabc"), "ok 0 1 1 1 2"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 3\nTransfer-Encoding: chunked\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\nContent-Length: 3\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 3\nContent-Length: 3\n\nabc"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 3,3\n\nabc"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: +3\n\nabc"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: -1\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length:\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 0x10\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 00000000000000000001\n\nx"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 0000000000000000001\n\nx"), "ok"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 4097\n\n"), "err 413"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 4096\n\n") + b"z" * 4096, "ok"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: gzip\n\n"), "err 501"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: gzip, chunked\n\n"), "err 501"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: Chunked\n\n0\n\n"), "ok 0 1 1 2"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\nTransfer-Encoding: chunked\n\n0\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.0\nTransfer-Encoding: chunked\n\n0\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n3\nabc\n0\n\n"), "ok"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n3;x=y\nabc\n0\n\n"), "ok"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n3 \nabc\n0\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n0000000000000003\nabc\n0\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n000000000000003\nabc\n0\n\n"), "ok"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n3\nabcX\n0\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n3\nab"), "err 499"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n" + c4097), "err 413"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n" + extbomb), "err 413"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n" + onebyte), "ok 0 1 1 2 2"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n" + trbomb), "err 413"),
        (s, crlf(b"HTTP/1.1 200 OK\nTransfer-Encoding: chunked\n\n" + extbomb), "err 413"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n0\nX-T: 1\n\n"), "ok"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n0\nX-T: 1\n x\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n0\nX-T 1\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\n0\n"), "err 499"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nTransfer-Encoding: chunked\n\nx\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\n" + big16 + b"\n"), "ok 0 1 1 0 16"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\n" + big17 + b"\n"), "err 431"),
        (q, crlf(long_head), "err 431"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nX: a\x00b\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nX: a\x7fb\n\n"), "err 400"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nX: a\x80b\n\n"), "ok"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nX: a\rb\n\n"), "err 400"),
        (q, crlf(b"GET /\x80 HTTP/1.1\nHost: a\n\n"), "err 400"),
        (q, crlf(b"GET /\x7f HTTP/1.1\nHost: a\n\n"), "err 400"),
        (q, crlf(b"OPTIONS * HTTP/1.1\nHost: a\n\n"), "ok"),
        (q, crlf(b"GET http://a/b HTTP/1.1\nHost: a\n\n"), "ok"),
        (q, crlf(b"GET a:b HTTP/1.1\nHost: a\n\n"), "err 400"),
        (q, crlf(b"CONNECT a:443 HTTP/1.1\nHost: a\n\n"), "ok"),
        (q, crlf(b"GET a:443 HTTP/1.1\nHost: a\n\n"), "err 400"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nExpect: 100-continue\nContent-Length: 1\n\nx"), "ok"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nExpect: foo\n\n"), "err 417"),
        (q, crlf(b"GET / HTTP/1.0\nExpect: foo\n\n"), "ok"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nConnection: close\n\n"), "ok 0 1 0"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\nConnection: keep-alive, Close\n\n"), "ok 0 1 0"),
        (q, crlf(b"GET / HTTP/1.1\nHost: a\n"), "err 499"),
        (q, crlf(b"POST / HTTP/1.1\nHost: a\nContent-Length: 5\n\nab"), "err 499"),
        (q, crlf(b"A" * 33 + b" / HTTP/1.1\nHost: a\n\n"), "err 400"),
        (q, crlf(b"A" * 32 + b" / HTTP/1.1\nHost: a\n\n"), "ok"),
        (s, crlf(b"HTTP/1.1 200 OK\nContent-Length: 2\n\nhi"), "ok 200 1 1 1 1"),
        (s, crlf(b"HTTP/1.1 200\nContent-Length: 0\n\n"), "err 400"),
        (s, crlf(b"HTTP/1.1 200 \nContent-Length: 0\n\n"), "ok 200"),
        (s, crlf(b"HTTP/1.1 099 x\nContent-Length: 0\n\n"), "err 400"),
        (s, crlf(b"HTTP/1.1 600 x\nContent-Length: 0\n\n"), "err 400"),
        (s, crlf(b"HTTP/1.1 599 x\nContent-Length: 0\n\n"), "ok 599"),
        (s, crlf(b"HTTP/1.1 2000 OK\nContent-Length: 0\n\n"), "err 400"),
        (s, crlf(b"HTTP/1.0 200 OK\n\n") + b"abc", "ok 200 0 0 3"),
        (s, crlf(b"HTTP/1.1 200 OK\n\n") + b"abc", "ok 200 1 0 3"),
        (s, crlf(b"HTTP/1.1 200 OK\n\n") + b"q" * 4096, "ok 200 1 0 3"),
        (s, crlf(b"HTTP/1.1 200 OK\n\n") + b"q" * 4097, "err 413"),
        (s, crlf(b"HTTP/1.1 204 No Content\nContent-Length: 5\n\n"), "ok 204 1 1 0"),
        (s, crlf(b"HTTP/1.1 304 Not Modified\nContent-Length: 5\n\n"), "ok 304 1 1 0"),
        (s, crlf(b"HTTP/1.1 305 Use Proxy\nContent-Length: 5\n\n"), "err 499"),
        (h, crlf(b"HTTP/1.1 200 OK\nContent-Length: 10\n\n"), "ok 200 1 1 0"),
        (s, crlf(b"HTTP/1.1 100 Continue\n\nHTTP/1.1 200 OK\nContent-Length: 1\n\nx"), "ok 200"),
        (s, crlf(b"HTTP/1.1 103 Early Hints\nLink: </a>\n\nHTTP/1.1 200 OK\nContent-Length: 1\n\nx"), "ok 200"),
        (s, crlf(b"HTTP/1.1 101 Switching Protocols\nUpgrade: x\n\n"), "ok 101 1 0 0"),
        (s, crlf(b"HTTP/1.1 100 Continue\n\n" * 17), "err 400"),
        (s, crlf(b"HTTP/1.1 200 OK\nTransfer-Encoding: chunked\n\n2\nhi\n0\n\n"), "ok 200 1 1 2"),
        (s, b"", "err 499"),
        (s, crlf(b"HTTP/1.1 200 OK\nContent-Length: 1\nTransfer-Encoding: chunked\n\n"), "err 400"),
        (s, crlf(b"HTTP/2 200 OK\n\n"), "err 400"),
        (s, crlf(b"HTTP/1.1 200 OK\nTransfer-Encoding: gzip\n\n"), "err 501"),
        (s, crlf(b"HTTP/1.1 200 \x80k\nContent-Length: 0\n\n"), "ok"),
        (s, crlf(b"HTTP/1.1 200 a\x00k\nContent-Length: 0\n\n"), "err 400"),
        (s, crlf(b"HTTP/1.0 200 OK\nTransfer-Encoding: chunked\n\n0\n\n"), "err 400"),
    ]
    return cases


NAMES = [b"Host", b"Accept", b"User-Agent", b"X-Req", b"Cache-Control", b"Link", b"Connection",
         b"Content-Type", b"Expect", b"content-length", b"Transfer-Encoding", b"X-Empty"]
VALUES = [b"a", b"text/event-stream", b"omega/1", b"no-cache", b"</p>; rel=next", b"close",
          b"keep-alive", b"application/json", b"100-continue", b"", b"  padded  ", b"\x80\xff"]
INTEREST = [0, 9, 10, 13, 32, 34, 44, 48, 58, 59, 70, 127, 128, 255]


def gen_body(rnd, framing, data):
    hdrs, wire = [], b""
    if framing == 1:
        hdrs.append(b"Content-Length: %d" % len(data))
        wire = data
    elif framing == 2:
        hdrs.append(b"Transfer-Encoding: chunked")
        p = 0
        while p < len(data):
            k = rnd.randint(1, max(1, min(700, len(data) - p)))
            ext = b";e=%d" % k if rnd.random() < 0.2 else b""
            if rnd.random() < 0.05:
                ext = b";x=" + b"y" * rnd.randint(100, 1500)
            wire += b"%x%s\r\n%s\r\n" % (k, ext, data[p:p + k])
            p += k
        wire += b"0\r\n"
        if rnd.random() < 0.2:
            wire += b"X-Trailer: t\r\n"
        wire += b"\r\n"
    return hdrs, wire


def gen_message(rnd):
    mode = rnd.choice("qqqssh")
    data = bytes(rnd.randrange(256) for _ in range(rnd.choice([0, 1, 5, 100, 1000, 3000, 4096, 5000])))
    framing = rnd.choice([0, 1, 1, 2, 2])
    hdrs, wire = gen_body(rnd, framing, data)
    fields = []
    for _ in range(rnd.randint(0, 6)):
        fields.append(rnd.choice(NAMES[1:9]) + b": " + rnd.choice(VALUES[:9]))
    if mode == "q":
        meth = rnd.choice([b"GET", b"POST", b"PUT", b"HEAD", b"OPTIONS", b"X-Do"])
        tgt = rnd.choice([b"/", b"/a/b?c=d", b"*", b"http://h/p", b"/%41"])
        ver = rnd.choice([b"HTTP/1.1"] * 6 + [b"HTTP/1.0"])
        start = meth + b" " + tgt + b" " + ver
        if ver == b"HTTP/1.1":
            fields.insert(rnd.randint(0, len(fields)), b"Host: h")
    else:
        st = rnd.choice([100, 101, 103, 200, 201, 204, 206, 304, 404, 500, 599])
        start = b"HTTP/1.%d %d R" % (rnd.choice([1, 1, 1, 0]), st)
        if st < 200 and st != 101:
            start = start + b"\r\n\r\nHTTP/1.1 200 OK"
    fields += hdrs
    if rnd.random() < 0.15:
        fields.append(b"Connection: close")
    return mode, start, fields, wire


def mutate(rnd, start, fields, wire):
    op = rnd.randrange(14)
    f = list(fields)
    if op == 0 and f:
        f.append(rnd.choice(f))                       # duplicate a field line
    elif op == 1:
        f.append(rnd.choice([b"Content-Length: 7", b"Transfer-Encoding: chunked",
                             b"Transfer-Encoding: identity", b"Content-Length: 1, 1"]))
    elif op == 2 and f:
        i = rnd.randrange(len(f))
        f[i] = f[i].replace(b":", b" :", 1)
    elif op == 3 and f:
        i = rnd.randrange(len(f))
        f[i] = rnd.choice([b" ", b"\t"]) + f[i]       # obs-fold
    elif op == 4:
        f.append(rnd.choice(NAMES) + b": " + rnd.choice(VALUES))
    elif op == 5:
        f += [b"X-%d: y" % i for i in range(rnd.randint(8, 20))]
    elif op == 6:
        start = start.replace(b" ", b"  ", 1)
    elif op == 7:
        start = start.replace(b"1.1", rnd.choice([b"1.2", b"2.0", b"1.x", b"11"]), 1)
    msg = start + b"\r\n" + b"".join(x + b"\r\n" for x in f) + b"\r\n" + wire
    b = bytearray(msg)
    if op == 8 and b:
        b[rnd.randrange(len(b))] = rnd.choice(INTEREST)
    elif op == 9:
        b.insert(rnd.randrange(len(b) + 1), rnd.choice(INTEREST))
    elif op == 10 and b:
        del b[rnd.randrange(len(b))]
    elif op == 11:
        b = bytearray(bytes(b).replace(b"\r\n", b"\n", rnd.randint(1, 3)))
    elif op == 12 and b:
        b = b[:rnd.randrange(len(b))]
    return bytes(b)


def fuzz_cases():
    rnd = random.Random(FUZZ_SEED)
    out = []
    for k in range(FUZZ_COUNT):
        mode, start, fields, wire = gen_message(rnd)
        if k % 4 == 0:
            msg = start + b"\r\n" + b"".join(x + b"\r\n" for x in fields) + b"\r\n" + wire
        else:
            msg = mutate(rnd, start, fields, wire)
            for _ in range(rnd.choice([0, 0, 1, 2])):
                msg = mutate_bytes(rnd, msg)
        out.append((mode, msg, None))
    return out


def mutate_bytes(rnd, msg):
    b = bytearray(msg)
    if b and rnd.random() < 0.5:
        b[rnd.randrange(len(b))] = rnd.choice(INTEREST)
    else:
        b.insert(rnd.randrange(len(b) + 1), rnd.choice(INTEREST))
    return bytes(b)


def corpus_bytes(cases):
    return b"".join(b"C %s %d\n" % (m.encode(), len(msg)) + msg + b"\n" for m, msg, _ in cases)


# ======== build and run

class Fail(Exception):
    pass


class NotRun(Exception):
    pass


def compile_unit(omc, out, lib_src, unit):
    src = b"om2\n" + lib_src + open(os.path.join(NET, unit), "rb").read()
    p = subprocess.run(NICE + [omc], input=src, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
    if p.returncode != 0:
        raise Fail("compile %s rc=%d %s" % (unit, p.returncode, p.stderr.decode(errors="replace").strip()))
    with open(out, "wb") as f:
        f.write(p.stdout)
    os.chmod(out, 0o755)
    return sha(src), sha(p.stdout)


def run_corpus(binary, corpus_path, ncases):
    with open(corpus_path, "rb") as f:
        p = subprocess.run(NICE + [binary], stdin=f, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
    lines = p.stdout.decode(errors="replace").splitlines()
    return p.returncode, lines


def stage_corpus(omc, work, lib_src, log):
    cases = [(m, msg, exp) for m, msg, exp in hand_cases()]
    nhand = len(cases)
    cases += fuzz_cases()
    expected, problems = [], []
    for i, (mode, msg, exp) in enumerate(cases):
        line, interim = reference(mode, msg)
        if exp is not None and not line.startswith(exp):
            problems.append("hand case %d: reference says %r, table says %r" % (i, line, exp))
        cc = cross_check(mode, msg, line, interim)
        if cc:
            problems.append("case %d (%s): %s; reference %r" % (i, mode, cc, line))
        expected.append("%d %s" % (i, line))
    if problems:
        raise Fail("reference self-check:\n  " + "\n  ".join(problems[:20]))
    data = corpus_bytes(cases)
    cpath = os.path.join(work, "corpus.bin")
    with open(cpath, "wb") as f:
        f.write(data)
    with open(os.path.join(work, "corpus.expected"), "w") as f:
        f.write("\n".join(expected) + "\n")
    binary = os.path.join(work, "parse")
    ssrc, sbin = compile_unit(omc, binary, lib_src, "tests/parse.om")
    rc, lines = run_corpus(binary, cpath, len(cases))
    bad = [(i, e, g) for i, (e, g) in enumerate(zip(expected, lines)) if e != g]
    log("corpus sha256=%s cases=%d hand=%d fuzz=%d parse.src=%s parse.bin=%s" % (
        sha(data), len(cases), nhand, len(cases) - nhand, ssrc, sbin))
    if rc != 0 or len(lines) != len(cases) or bad:
        detail = "; ".join("want %r got %r" % (e, g) for _, e, g in bad[:8])
        raise Fail("corpus rc=%d lines=%d/%d mismatches=%d %s" % (rc, len(lines), len(cases), len(bad), detail))
    log("http.client parity on responses: agree=%(agree)d both_reject=%(both_reject)d "
        "reference_stricter=%(stricter)d skipped=%(skipped)d" % PARITY)
    dist = {}
    for e in expected:
        k = " ".join(e.split()[1:3]) if " err " in e else e.split()[1]
        dist[k] = dist.get(k, 0) + 1
    log("outcomes: " + " ".join("%s=%d" % (k, dist[k]) for k in sorted(dist)))
    log("HTTP-CORPUS PASS cases=%d" % len(cases))
    return cpath, expected


MUTANTS = [
    ("CL+TE accepted", "if and cl te", "if and cl cl"),
    ("chunk size decimal", "v = add mul v 16 h", "v = add mul v 10 h"),
    ("field cap off by one", "if or ge n get cs HHMAX ge n HHSLOTS", "if or gt n get cs HHMAX ge n HHSLOTS"),
    ("304 has a body", "if or or eq get cs HISHEAD 1 lt s 200 or eq s 204 eq s 304",
     "if or or eq get cs HISHEAD 1 lt s 200 or eq s 204 eq s 305"),
    ("status range", "if or lt c 100 gt c 599", "if or lt c 100 gt c 999"),
    ("Expect value ignored", "if not hieq view part io vo vl hv100c", "if hieq view part io vo vl hv100c"),
    ("duplicate TE flag", "if ne and get cs HFLAGS 2 0", "if ne and get cs HFLAGS 1 0"),
    ("HTTP/1.0 read as 1.1", "if eq mn 48", "if eq mn 49"),
    ("close body cap", "if gt add got n cap", "if ge add got n cap"),
    ("bare LF accepted", "if ne get io sub i 1 13", "if eq get io sub i 1 10"),
    ("F4 chunk framing uncapped", "if gt f add get cs HFRMAX mul get cs HBGOT 8", "if eq f 0"),
]


def stage_mutants(omc, work, lib_src, cpath, expected, log):
    text = lib_src.decode()
    lines = text.split("\n")
    for name, old, new in MUTANTS:
        hits = [i for i, l in enumerate(lines) if l == old]
        if len(hits) != 1:
            raise Fail("mutant %r: line %r occurs %d times (must be exactly once)" % (name, old, len(hits)))
        ml = list(lines)
        ml[hits[0]] = new
        mbin = os.path.join(work, "parse-mutant")
        try:
            compile_unit(omc, mbin, "\n".join(ml).encode(), "tests/parse.om")
        except Fail as e:
            raise Fail("mutant %r does not compile (invalid mutant): %s" % (name, e))
        rc, got = run_corpus(mbin, cpath, len(expected))
        if rc == 0 and got == expected:
            raise Fail("mutant %r SURVIVED: the corpus does not detect it" % name)
        log("mutant killed: %s (line %d)" % (name, hits[0] + 1))
    log("HTTP-MUTANTS PASS killed=%d/%d" % (len(MUTANTS), len(MUTANTS)))


def stage_live(omc, work, lib_src, log):
    binary = os.path.join(work, "live")
    ssrc, sbin = compile_unit(omc, binary, lib_src, "tests/live.om")
    p = subprocess.run(NICE + [binary], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
    log("live.src=%s live.bin=%s rc=%d" % (ssrc, sbin, p.returncode))
    if p.returncode != 0:
        raise Fail("tests/live.om exit %d (see the ret numbers in live.om)" % p.returncode)
    log("HTTP-LIVE PASS")


LIVE_MUTANTS = [
    ("head deadline never armed", "put cs HDL add ulong monons get cs HHDLV", "put cs HDL 0"),
    ("hclose leaks the descriptor", "ret fclose int get cs HFD", "ret 0"),
    ("SSE data CR not a line break", "if eq get v q 13", "if eq get v q 14"),
    ("chunk-size digit mask", "put io add b i hhexd and x 15", "put io add b i hhexd and x 7"),
    ("F1 no body deadline after the head", "put cs HDL hbdl cs", "put cs HDL 0"),
    ("F2 Content-Length overrun buffered", "ret gt add get cs HWOUT n get cs HWCL", "ret false"),
    ("F3 100 Continue keyed on HWLEN", "if or ne get cs HFINAL 0 ne get cs HMEM 0",
     "if or ne get cs HWLEN 0 ne get cs HMEM 0"),
]


def stage_live_mutants(omc, work, lib_src, log):
    """Negative controls for tests/live.om: each mutant of http.om must make it fail."""
    lines = lib_src.decode().split("\n")
    for name, old, new in LIVE_MUTANTS:
        hits = [i for i, l in enumerate(lines) if l == old]
        if len(hits) != 1:
            raise Fail("live mutant %r: line %r occurs %d times (must be exactly once)" % (name, old, len(hits)))
        ml = list(lines)
        ml[hits[0]] = new
        mbin = os.path.join(work, "live-mutant")
        try:
            compile_unit(omc, mbin, "\n".join(ml).encode(), "tests/live.om")
        except Fail as e:
            raise Fail("live mutant %r does not compile (invalid mutant): %s" % (name, e))
        try:
            p = subprocess.run(NICE + [mbin], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
        if rc == 0:
            raise Fail("live mutant %r SURVIVED: tests/live.om exits 0" % name)
        log("live mutant killed: %s (line %d, exit %s)" % (name, hits[0] + 1, rc))
    log("HTTP-LIVE-MUTANTS PASS killed=%d/%d" % (len(LIVE_MUTANTS), len(LIVE_MUTANTS)))



EXPECTED_SSE = (b"id: 1\ndata: hello from Omega\n\n"
                b"event: multi\nid: 2\ndata: line one\ndata: line two\n\n"
                b"id: 3\ndata: bye\n\n")
EXPECTED_CLIENT = (b"event= id=1 data=hello from Omega\n"
                   b"event=multi id=2 data=line one\\nline two\n"
                   b"event= id=3 data=bye\n")
HEALTH = b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n\r\n{"ok":true}'
BAD = b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
E2E_CONNS = 9


def fdcount(pid):
    return len(os.listdir("/proc/%d/fd" % pid))


def settle(pid, base):
    t = time.monotonic() + 3
    while time.monotonic() < t:
        if fdcount(pid) == base:
            return
        time.sleep(0.05)
    raise Fail("server fd count %d, expected %d after the connection closed (leak)" % (fdcount(pid), base))


def raw_exchange(port, data):
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    try:
        if data:
            s.sendall(data)
        s.shutdown(socket.SHUT_WR)
        out = b""
        while True:
            k = s.recv(65536)
            if not k:
                return out
            out += k
    finally:
        s.close()


def curl(args, timeout=30):
    p = subprocess.run(NICE + ["curl", "-sS"] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    if p.returncode != 0:
        raise Fail("curl %s rc=%d %s" % (" ".join(args), p.returncode, p.stderr.decode(errors="replace")))
    return p.stdout


def e2e_checks(port, pid, base, work, client_bin):
    url = "http://127.0.0.1:%d" % port
    got = curl(["-N", "--max-time", "10", url + "/events"])
    if got != EXPECTED_SSE:
        raise Fail("curl /events: %r" % got[:200])
    settle(pid, base)
    got = curl(["-i", "--max-time", "10", url + "/health"])
    if got != HEALTH:
        raise Fail("curl -i /health: %r" % got[:200])
    settle(pid, base)
    blob = random.Random(300).randbytes(300 * 1024)
    bpath = os.path.join(work, "blob.bin")
    with open(bpath, "wb") as f:
        f.write(blob)
    got = curl(["--max-time", "20", "--data-binary", "@" + bpath, url + "/echo"])
    if got != blob:
        raise Fail("curl 300 KiB echo (Expect: 100-continue) differs, %d bytes" % len(got))
    settle(pid, base)
    got = curl(["--max-time", "20", "-H", "Transfer-Encoding: chunked", "--data-binary", "@" + bpath, url + "/echo"])
    if got != blob:
        raise Fail("curl chunked 300 KiB echo differs, %d bytes" % len(got))
    settle(pid, base)
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    c.request("GET", "/health")
    r = c.getresponse()
    if r.status != 200 or r.read() != b'{"ok":true}':
        raise Fail("http.client GET /health")
    s0 = c.sock
    c.request("HEAD", "/health")
    r = c.getresponse()
    if r.status != 200 or r.getheader("Content-Length") != "11" or r.read() != b"":
        raise Fail("http.client HEAD /health")
    c.request("POST", "/echo", body=blob[:1000])
    r = c.getresponse()
    if r.status != 200 or r.read() != blob[:1000]:
        raise Fail("http.client POST /echo")
    c.request("GET", "/nope")
    r = c.getresponse()
    if r.status != 404 or r.read() != b"" or c.sock is not s0:
        raise Fail("http.client 404 on the same keep-alive connection")
    c.request("DELETE", "/echo")
    r = c.getresponse()
    if r.status != 405 or r.read() != b"" or c.sock is not s0:
        raise Fail("http.client 405 on the same keep-alive connection")
    c.request("GET", "/health", headers={"Connection": "close"})
    r = c.getresponse()
    if r.status != 200 or r.getheader("Connection") != "close" or r.read() != b'{"ok":true}':
        raise Fail("http.client GET /health with Connection: close")
    c.close()
    settle(pid, base)
    got = raw_exchange(port, b"GET / HTTP/1.1\r\nHost: a\r\nX: 1\r\n folded\r\n\r\n")
    if got != BAD:
        raise Fail("obs-fold request answered %r" % got[:200])
    settle(pid, base)
    got = raw_exchange(port, b"")
    if got != b"":
        raise Fail("empty connection answered %r" % got[:200])
    settle(pid, base)
    p = subprocess.run(NICE + [client_bin, str(port), "/events"], stdout=subprocess.PIPE, timeout=30)
    if p.returncode != 0 or p.stdout != EXPECTED_CLIENT:
        raise Fail("sse_client rc=%d out=%r" % (p.returncode, p.stdout[:300]))
    settle(pid, base)
    got = raw_exchange(port, b"GET /health HTTP/1.0\r\n\r\n")
    want = (b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 11\r\n'
            b'Connection: close\r\n\r\n{"ok":true}')
    if got != want:
        raise Fail("HTTP/1.0 request answered %r" % got[:200])


def stage_e2e(omc, work, lib_src, log):
    if not shutil.which("curl"):
        raise NotRun("curl is not installed")
    sbin = os.path.join(work, "sse_server")
    cbin = os.path.join(work, "sse_client")
    s1 = compile_unit(omc, sbin, lib_src, "examples/sse_server.om")
    s2 = compile_unit(omc, cbin, lib_src, "examples/sse_client.om")
    log("sse_server src=%s bin=%s; sse_client src=%s bin=%s" % (s1 + s2))
    srv = subprocess.Popen(NICE + [sbin, str(E2E_CONNS)], stdout=subprocess.PIPE)
    try:
        line = srv.stdout.readline()
        if not line.startswith(b"PORT "):
            raise Fail("sse_server printed %r" % line)
        port = int(line[5:])
        if port == 18091 or port == 0:
            raise Fail("unexpected port %d" % port)
        base = fdcount(srv.pid)
        e2e_checks(port, srv.pid, base, work, cbin)
        rc = srv.wait(timeout=15)
        if rc != 0:
            raise Fail("sse_server exit %d" % rc)
    finally:
        if srv.poll() is None:
            srv.kill()
            srv.wait()
    log("HTTP-E2E PASS connections=%d (curl, http.client, raw, sse_client)" % E2E_CONNS)


def main():
    args = sys.argv[1:]
    omc = os.path.join(ROOT, "seed", "out", "omc")
    keep = None
    while args:
        a = args.pop(0)
        if a == "--omc" and args:
            omc = args.pop(0)
        elif a == "--keep" and args:
            keep = args.pop(0)
        else:
            print(__doc__)
            return 3
    if not os.access(omc, os.X_OK):
        print("NOT_RUN: no compiler at %s" % omc)
        return 3
    lib_src = open(LIB, "rb").read()
    work = keep or tempfile.mkdtemp(prefix="http-check-")
    os.makedirs(work, exist_ok=True)
    out = []

    def log(s):
        out.append(s)
        print(s, flush=True)

    log("omc sha256=%s http.om sha256=%s" % (sha(open(omc, "rb").read()), sha(lib_src)))
    status = 0
    try:
        cpath, expected = stage_corpus(omc, work, lib_src, log)
        stage_mutants(omc, work, lib_src, cpath, expected, log)
        stage_live(omc, work, lib_src, log)
        stage_live_mutants(omc, work, lib_src, log)
        stage_e2e(omc, work, lib_src, log)
        log("HTTP-CHECK PASS")
    except NotRun as e:
        log("HTTP-CHECK NOT_RUN %s" % e)
        status = 3
    except (Fail, subprocess.TimeoutExpired) as e:
        log("HTTP-CHECK FAIL %s" % e)
        status = 1
    finally:
        if keep is None:
            shutil.rmtree(work, ignore_errors=True)
    return status


if __name__ == "__main__":
    sys.exit(main())
