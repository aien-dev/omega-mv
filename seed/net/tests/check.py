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
http.client, examples/sse_client.om, server fd count), https (tests/https.om = http.om +
https.om + om/tls against a Python ssl TLS 1.3 server and openssl s_server with a throwaway PKI:
bodies, chunked, close-delimited, truncation, wrong name, untrusted issuer, deadline; mutants
of the glue and of the notls stub; with TLS_NET=1 two live read-only GETs).
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
NOTLS = os.path.join(NET, "notls.om")
HTTPS = os.path.join(NET, "https.om")
TLS_UNITS = ["sha2", "hkdf", "drbg", "x25519", "sha3", "mlkem", "chacha", "aesgcm", "ec", "ecdh", "rsa", "x509", "rev", "fips", "tls"]
TLSDIR = os.path.join(ROOT, "om", "tls")

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
    os.chmod(out, 0o700)
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


# ======== https: http.om + https.om + om/tls against TLS 1.3 servers with a throwaway PKI
def ossl(work, *args):
    p = subprocess.run(["openssl"] + list(args), cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    if p.returncode != 0:
        raise Fail("openssl %s rc=%d %s" % (" ".join(args), p.returncode, p.stderr.decode(errors="replace").strip()))


HTTPS_CA_CNF = """[ca]
default_ca = c
[c]
database = index.txt
new_certs_dir = issued
serial = serial
default_md = sha256
policy = p
copy_extensions = copy
unique_subject = no
[p]
commonName = supplied
"""


def https_pki(work):
    """root -> leaf for localhost (EC P-256); a leaf for another name; an unrelated root's leaf;
    leaves of root that expired (2025) or are not valid yet (2048); a self-signed localhost leaf."""
    d = os.path.join(work, "pki")
    os.makedirs(os.path.join(d, "issued"), exist_ok=True)
    ca_ext = ["-addext", "basicConstraints=critical,CA:true", "-addext", "keyUsage=critical,keyCertSign,cRLSign"]
    leaf_ext = ["-addext", "basicConstraints=critical,CA:false", "-addext", "keyUsage=critical,digitalSignature",
                "-addext", "extendedKeyUsage=serverAuth"]
    for root in ("root", "other"):
        ossl(d, "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", root + ".key")
        ossl(d, "req", "-x509", "-new", "-key", root + ".key", "-subj", "/CN=http-check-" + root, "-days", "30",
             "-sha256", *ca_ext, "-out", root + ".pem")
    with open(os.path.join(d, "ca.cnf"), "w") as f:
        f.write(HTTPS_CA_CNF)
    open(os.path.join(d, "index.txt"), "w").close()
    with open(os.path.join(d, "serial"), "w") as f:
        f.write("1000\n")
    for name, san, root in (("leaf", "localhost", "root"), ("wrong", "other.example", "root"), ("stray", "localhost", "other"),
                            ("expired", "localhost", "root"), ("notyet", "localhost", "root"), ("self", "localhost", None)):
        ossl(d, "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", name + ".key")
        if root is None:
            ossl(d, "req", "-x509", "-new", "-key", name + ".key", "-subj", "/CN=" + san, "-addext", "subjectAltName=DNS:" + san,
                 *leaf_ext, "-days", "10", "-sha256", "-out", name + ".pem")
            continue
        ossl(d, "req", "-new", "-key", name + ".key", "-subj", "/CN=" + san, "-addext", "subjectAltName=DNS:" + san,
             *leaf_ext, "-out", name + ".csr")
        if name in ("expired", "notyet"):
            start, end = ("250101000000Z", "250201000000Z") if name == "expired" else ("480101000000Z", "490101000000Z")
            ossl(d, "ca", "-batch", "-config", "ca.cnf", "-cert", root + ".pem", "-keyfile", root + ".key", "-in", name + ".csr",
                 "-startdate", start, "-enddate", end, "-notext", "-out", name + ".pem")
            continue
        ossl(d, "x509", "-req", "-in", name + ".csr", "-CA", root + ".pem", "-CAkey", root + ".key", "-CAcreateserial",
             "-days", "10", "-copy_extensions", "copy", "-out", name + ".pem")
    return d


HTTPS_BIG = bytes((i * 7 + 3) & 255 for i in range(307200))
HTTPS_CHUNKS = [1, 17, 4096, 16384, 16385, 3, 60000, 100]
HTTPS_FRAG = [1, 2, 3, 5, 7, 11]
HTTPS_SEEN = {}        # Python TLS server name -> request heads it received
HTTPS_PLAIN_SEEN = []  # first bytes each connection brought to the plain HTTP server


def https_body(path):
    """(response bytes, body the client must print, close mode) for one path of the Python server.
    close mode: 'notify' = close_notify then close, 'raw' = close the socket without close_notify,
    'frag' = the response in TLS records of 1..11 bytes, 'partial' = an incomplete TLS record after the
    head then a raw close, 'dribble' = the body one byte every 250 ms, 'silent' = no answer."""
    if path == "/small":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 6\r\nConnection: close\r\n\r\nhello\n", b"hello\n", "notify"
    if path == "/big":
        return b"HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n" % len(HTTPS_BIG) + HTTPS_BIG, HTTPS_BIG, "notify"
    if path == "/chunked":
        body = HTTPS_BIG[:100000]
        out = [b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"]
        pos = 0
        i = 0
        while pos < len(body):
            n = min(HTTPS_CHUNKS[i % len(HTTPS_CHUNKS)], len(body) - pos)
            out.append(b"%x\r\n" % n + body[pos:pos + n] + b"\r\n")
            pos += n
            i += 1
        out.append(b"0\r\n\r\n")
        return b"".join(out), body, "notify"
    if path == "/frag":
        body = HTTPS_BIG[:3000]
        return b"HTTP/1.1 200 OK\r\nContent-Length: 3000\r\nConnection: close\r\n\r\n" + body, body, "frag"
    if path == "/close":
        return b"HTTP/1.0 200 OK\r\n\r\n" + HTTPS_BIG[:50000], HTTPS_BIG[:50000], "notify"
    if path == "/closeraw":
        return b"HTTP/1.0 200 OK\r\n\r\n" + HTTPS_BIG[:5000], HTTPS_BIG[:5000], "raw"
    if path == "/trunc":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n0123456789", b"0123456789", "raw"
    if path == "/partial":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n", b"", "partial"
    if path == "/bighead":
        return b"HTTP/1.1 200 OK\r\nX-Pad: " + b"a" * 20000 + b"\r\nContent-Length: 0\r\n\r\n", b"", "notify"
    if path == "/dribble":
        return b"HTTP/1.1 200 OK\r\nContent-Length: 40\r\nConnection: close\r\n\r\n" + b"d" * 40, b"d" * 40, "dribble"
    if path == "/silent":
        return b"", b"", "silent"
    return b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", b"", "notify"


def https_serve(conn, name):
    """One connection of the Python TLS server: read a request head, answer by path."""
    import ssl
    try:
        conn.settimeout(10)
        head = b""
        while b"\r\n\r\n" not in head and len(head) < 65536:
            b = conn.recv(4096)
            if not b:
                return
            head += b
        HTTPS_SEEN[name] = HTTPS_SEEN.get(name, 0) + 1
        path = head.split(b" ", 2)[1].decode()
        resp, _, mode = https_body(path)
        if mode == "silent":
            time.sleep(4)
            conn.close()
            return
        if mode == "frag":
            pos = i = 0
            while pos < len(resp):
                n = HTTPS_FRAG[i % len(HTTPS_FRAG)]
                conn.sendall(resp[pos:pos + n])
                pos += n
                i += 1
        elif mode == "dribble":
            k = resp.index(b"\r\n\r\n") + 4
            conn.sendall(resp[:k])
            for j in range(k, len(resp)):
                time.sleep(0.25)
                conn.sendall(resp[j:j + 1])
        else:
            conn.sendall(resp)
        if mode in ("raw", "partial"):
            fd = conn.detach()
            if mode == "partial":
                # an application_data record header announcing 100 bytes, then only 10 of them
                os.write(fd, b"\x17\x03\x03\x00\x64" + b"\x00" * 10)
            os.close(fd)
            return
        try:
            conn.unwrap()
        except (OSError, ssl.SSLError):
            pass
        conn.close()
    except (OSError, ssl.SSLError):
        try:
            conn.close()
        except OSError:
            pass


def https_listen(stop, on_conn):
    """A listener on 127.0.0.1 (ephemeral port) in a thread; on_conn(socket) per connection in its own thread."""
    import threading
    ls = socket.socket()
    ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", 0))
    ls.listen(16)
    ls.settimeout(0.5)
    port = ls.getsockname()[1]

    def loop():
        while not stop.is_set():
            try:
                c, _ = ls.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            threading.Thread(target=on_conn, args=(c,), daemon=True).start()
        ls.close()
    threading.Thread(target=loop, daemon=True).start()
    return port


def https_plain(c):
    """The plain HTTP server: record what the client sent first, answer in cleartext, close."""
    try:
        c.settimeout(5)
        first = c.recv(4096)
        HTTPS_PLAIN_SEEN.append(first)
        c.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 6\r\nConnection: close\r\n\r\nhello\n")
    except OSError:
        pass
    c.close()


def https_hsilent(c):
    """A TCP peer that accepts and never answers the ClientHello."""
    time.sleep(6)
    c.close()


def https_server(pki, cert, stop):
    """A Python TLS 1.3 server with certificate cert on 127.0.0.1; returns its port."""
    import ssl
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_3
    ctx.load_cert_chain(os.path.join(pki, cert + ".pem"), os.path.join(pki, cert + ".key"))
    ctx.set_alpn_protocols(["http/1.1"])

    def on_conn(c):
        try:
            c.settimeout(10)
            tc = ctx.wrap_socket(c, server_side=True)
        except (OSError, ssl.SSLError):
            c.close()
            return
        https_serve(tc, cert)
    return https_listen(stop, on_conn)


def https_ossl(pki, cert):
    """openssl s_server -www -tls1_3 with certificate cert; returns (port, process)."""
    # openssl s_server cannot report a kernel-chosen port: pick a free one and bind it
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    p = subprocess.Popen(["openssl", "s_server", "-accept", "127.0.0.1:%d" % port, "-cert", os.path.join(pki, cert + ".pem"),
                          "-key", os.path.join(pki, cert + ".key"), "-www", "-tls1_3"],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.2).close()
            break
        except OSError:
            time.sleep(0.1)
    return port, p


def https_run(binary, host, port, store, path, connect="127.0.0.1", idle=None, total=None, spare=None, timeout=20):
    """Run tests/https.om; (exit, body, status or None, HTTPSERR code or None, TLSDETAIL or None)."""
    args = [binary, host, str(port), store, path, connect]
    for opt in (idle, total, spare):
        if opt is None:
            break
        args.append(str(opt))
    if spare is not None and total is None:
        raise Fail("https_run: IOSPARE needs TOTALMS (positional)")
    try:
        p = subprocess.run(NICE + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout", b"", None, None, None
    st = err = det = None
    for l in p.stderr.decode(errors="replace").splitlines():
        if l.startswith("status "):
            st = int(l[7:])
        if l.startswith("HTTPSERR "):
            err = int(l[9:])
        if l.startswith("TLSDETAIL "):
            det = int(l[10:])
    return p.returncode, p.stdout, st, err, det


HETLS = -600
HETIME = -110
HEINVAL = -22
HEHEAD = -431
X5EEXP, X5ENYV, X5ENAME, X5EUNK = 2, 3, 4, 5   # om/tls/x509.om reject codes (x509_errname)


def R(name, path, srv, rc, err=None, det=None, idle=None, total=None, spare=None, status=None, noreq=False, timeout=20):
    """One row: server srv (a Python TLS server by certificate, 'o:<cert>' = openssl s_server -www,
    'plain' = cleartext HTTP, 'hsilent' = TCP peer that never answers), expected exit, HTTPSERR,
    TLSDETAIL, status; noreq = the server must not receive a request (refused before any app byte)."""
    return dict(name=name, path=path, srv=srv, rc=rc, err=err, det=det, idle=idle, total=total, spare=spare,
                status=status, noreq=noreq, timeout=timeout)


HTTPS_ROWS = [
    R("small body", "/small", "leaf", 0),
    R("300 KiB Content-Length body", "/big", "leaf", 0),
    R("chunked body", "/chunked", "leaf", 0),
    R("fragmented: response in TLS records of 1..11 bytes", "/frag", "leaf", 0),
    R("close-delimited body (close_notify ends it)", "/close", "leaf", 0),
    R("smallest io (TLCSZ + 512): chunked body", "/chunked", "leaf", 0, idle=30000, total=0, spare=512),
    R("io below TLCSZ + 512: htlsinit refuses", "/small", "leaf", 4, HEINVAL, idle=30000, total=0, spare=511, noreq=True),
    R("response head over the head limit", "/bighead", "leaf", 4, HEHEAD),
    R("truncation: TCP close without close_notify is an error", "/trunc", "leaf", 4, HETLS - 4, status=200),
    R("close-delimited body ended by TCP close, no close_notify", "/closeraw", "leaf", 4, HETLS - 4, status=200),
    R("incomplete TLS record then TCP close", "/partial", "leaf", 4, HETLS - 4, status=200),
    R("silent server: idle limit through tls_deadline", "/silent", "leaf", 4, HETIME, idle=500, timeout=10),
    R("one deadline: body dribble cut at TOTALMS 1500", "/dribble", "leaf", 4, HETIME, idle=2000, total=1500, status=200, timeout=15),
    R("silent handshake: idle limit bounds tls_connect", "/small", "hsilent", 4, HETIME, idle=500, timeout=10),
    R("silent handshake: TOTALMS bounds tls_connect", "/small", "hsilent", 4, HETIME, idle=30000, total=800, timeout=10),
    R("plain HTTP server: bad record (TLEREC), no cleartext fallback", "/small", "plain", 4, HETLS - 5),
    R("wrong name in the certificate", "/small", "wrong", 4, HETLS - 17, X5ENAME, noreq=True),
    R("leaf of an untrusted root", "/small", "stray", 4, HETLS - 17, X5EUNK, noreq=True),
    R("expired leaf", "/small", "expired", 4, HETLS - 17, X5EEXP, noreq=True),
    R("leaf not valid yet", "/small", "notyet", 4, HETLS - 17, X5ENYV, noreq=True),
    R("self-signed leaf", "/small", "self", 4, HETLS - 17, X5EUNK, noreq=True),
    R("openssl s_server -www (HTTP/1.0, close-delimited)", "/openssl-www", "o:leaf", 0),
    R("openssl s_server: wrong name", "/openssl-www", "o:wrong", 4, HETLS - 17, X5ENAME),
    R("openssl s_server: untrusted root", "/openssl-www", "o:stray", 4, HETLS - 17, X5EUNK),
    R("openssl s_server: expired leaf", "/openssl-www", "o:expired", 4, HETLS - 17, X5EEXP),
]
HTTPS_PY_CERTS = ("leaf", "wrong", "stray", "expired", "notyet", "self")
HTTPS_OSSL_CERTS = ("leaf", "wrong", "stray", "expired")


def https_compose(src_lines, notls=False, over=None):
    """The library source in front of tests/https.om or tests/nosup.om; over = (unit, text) replaces one om/tls unit."""
    if notls:
        return src_lines
    units = b"".join(over[1].encode() if over and over[0] == u else open(os.path.join(TLSDIR, u + ".om"), "rb").read()
                     for u in TLS_UNITS)
    return units + src_lines


def https_integ(omc, binary):
    """om/tls/build.sh -p: write the fips.om integrity HMAC into a binary that composes om/tls;
    without it tls_connect fails closed (TLS_ERR_SELF_TEST detail 16)."""
    p = subprocess.run(NICE + ["sh", os.path.join(TLSDIR, "build.sh"), "-p", omc, binary],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=600)
    if p.returncode != 0:
        raise Fail("integrity step on %s rc=%d %s" % (binary, p.returncode, p.stderr.decode(errors="replace").strip()))


def https_mutate(text, fn, old, new):
    """Replace the first line equal to old after the line 'fn <fn> ...' (exactly one hit in that function)."""
    lines = text.split("\n")
    start = [i for i, l in enumerate(lines) if l.startswith("fn " + fn + " ")]
    if len(start) != 1:
        raise Fail("mutant: function %r found %d times" % (fn, len(start)))
    end = start[0] + 1
    while end < len(lines) and not lines[end].startswith("fn "):
        end += 1
    hits = [i for i in range(start[0], end) if lines[i] == old]
    if len(hits) != 1:
        raise Fail("mutant: line %r occurs %d times in %s (must be exactly once)" % (old, len(hits), fn))
    lines[hits[0]] = new
    return "\n".join(lines), hits[0] + 1


def https_row(binary, pki, port, row):
    """Run one row; (ok, rc, status, err, detail, body length)."""
    root = os.path.join(pki, "root.pem")
    srv = row["srv"]
    seen = HTTPS_SEEN.get(srv, 0)
    plain0 = len(HTTPS_PLAIN_SEEN)
    rc, body, st, err, det = https_run(binary, "localhost", port[srv], root, row["path"], idle=row["idle"],
                                       total=row["total"], spare=row["spare"], timeout=row["timeout"])
    ok = rc == row["rc"] and err == row["err"] and det == row["det"]
    if row["rc"] == 0:
        if srv.startswith("o:"):
            ok = ok and st == 200 and b"Ciphers supported in s_server binary" in body and body.rstrip(b"\r\n").endswith(b"</HTML>")
        else:
            ok = ok and st == 200 and body == https_body(row["path"])[1]
    if row["status"] is not None:
        ok = ok and st == row["status"]
    if row["noreq"]:
        ok = ok and HTTPS_SEEN.get(srv, 0) == seen
    if srv == "plain":
        # what reached the cleartext server must be a TLS handshake record, never an HTTP request
        got = HTTPS_PLAIN_SEEN[plain0:]
        ok = ok and len(got) == 1 and got[0][:2] == b"\x16\x03" and b"GET" not in got[0]
    return ok, rc, st, err, det, len(body)


def https_rows(binary, pki, port, log, label):
    """All rows; the name of the first row that fails, or None."""
    for row in HTTPS_ROWS:
        ok, rc, st, err, det, n = https_row(binary, pki, port, row)
        log("%s row %-58s rc=%s status=%s err=%s detail=%s body=%d -> %s" % (label, row["name"], rc, st, err, det, n, "ok" if ok else "MISMATCH"))
        if not ok:
            return row["name"]
    return None


HTTPS_MUTANTS = [
    # (name, unit, function, old line, new line)
    ("http.om: read dispatch to TLS gone", "http", "hrecvio", "ret htlsrecv cs io off n", "ret HENOSUP"),
    ("https.om: head read error taken as end of stream", "https", "htlsrecv", "ret htlsmap neg r", "ret 0"),
    ("https.om: body truncation taken as end of stream", "https", "htlsrecvd", "ret htlsmap neg r", "ret 0"),
    ("https.om: http.om wait policy not given to TLS", "https", "htlsarm", "tls_deadline part io b TLCSZ long dl", "tls_deadline part io b TLCSZ 0"),
    ("https.om: records not written", "https", "htlswr", "r = tls_write part io get cs HTB TLCSZ part bb 0 k", "r = long 0"),
    ("https.om: handshake not under the wait policy", "https", "htlsconn", "htlsarm cs io", "b = get cs HTB"),
    ("https.om: exchange deadline HXDL ignored", "https", "htlsarm", "dl = x", "x = ulong 0"),
    ("https.om: io minimum not enforced", "https", "htlsinit", "if lt len io add TLCSZ 512", "if lt len io TLCSZ"),
    ("https.om: refusal reason lost", "https", "htlsdet", "ret tls_detail part io get cs HTB TLCSZ", "ret 0"),
    ("tls.om: chain verification result ignored", "tls", "tls_chain", "if ne r 0", "if eq r 999999"),
    ("x509.om: notAfter not checked", "x509", "chain_time", "ret X5EEXP", "ret 0"),
    ("x509.om: notBefore not checked", "x509", "chain_time", "ret X5ENYV", "ret 0"),
    ("x509.om: host name not checked", "x509", "chain_host", "ret X5ENAME", "ret 0"),
]


def stage_https(omc, work, http_src, notls_src, log):
    import threading
    if not shutil.which("openssl"):
        raise NotRun("openssl is not installed")
    for u in TLS_UNITS:
        if not os.path.exists(os.path.join(TLSDIR, u + ".om")):
            raise NotRun("om/tls/%s.om is missing" % u)
    https_src = open(HTTPS, "rb").read()
    pki = https_pki(work)
    # the program without TLS refuses every byte on a non-TCP transport
    nbin = os.path.join(work, "nosup")
    compile_unit(omc, nbin, http_src + notls_src, "tests/nosup.om")
    p = subprocess.run(NICE + [nbin], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if p.returncode != 0:
        raise Fail("tests/nosup.om exit %d: a notls program moved bytes on HXPORT 1" % p.returncode)
    log("https notls control: nosup exit 0")
    mt, ln = https_mutate(notls_src.decode(), "htlsrecv", "ret HENOSUP", "ret 0")
    compile_unit(omc, nbin, http_src + mt.encode(), "tests/nosup.om")
    p = subprocess.run(NICE + [nbin], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    if p.returncode == 0:
        raise Fail("notls mutant SURVIVED: notls.om htlsrecv returning 0 passes nosup")
    log("https notls mutant killed: htlsrecv ret 0 (notls.om line %d, exit %d)" % (ln, p.returncode))
    # the real program
    hbin = os.path.join(work, "https")
    ssrc, sbin = compile_unit(omc, hbin, https_compose(http_src + https_src), "tests/https.om")
    https_integ(omc, hbin)
    sbin = sha(open(hbin, "rb").read())
    log("https.src=%s https.bin=%s https.om sha256=%s" % (ssrc, sbin, sha(https_src)))
    stop = threading.Event()
    port = {}
    for cert in HTTPS_PY_CERTS:
        port[cert] = https_server(pki, cert, stop)
    port["plain"] = https_listen(stop, https_plain)
    port["hsilent"] = https_listen(stop, https_hsilent)
    osrv = []
    try:
        for cert in HTTPS_OSSL_CERTS:
            p, proc = https_ossl(pki, cert)
            osrv.append(proc)
            port["o:" + cert] = p
        bad = https_rows(hbin, pki, port, log, "https")
        if bad:
            raise Fail("https row %r" % bad)
        log("HTTPS-ROWS PASS rows=%d" % len(HTTPS_ROWS))
        mbin = os.path.join(work, "https-mutant")
        for name, unit, fn, old, new in HTTPS_MUTANTS:
            over = None
            if unit == "http":
                mt, ln = https_mutate(http_src.decode(), fn, old, new)
                lib = mt.encode() + https_src
            elif unit == "https":
                mt, ln = https_mutate(https_src.decode(), fn, old, new)
                lib = http_src + mt.encode()
            else:
                mt, ln = https_mutate(open(os.path.join(TLSDIR, unit + ".om"), "rb").read().decode(), fn, old, new)
                lib = http_src + https_src
                over = (unit, mt)
            try:
                compile_unit(omc, mbin, https_compose(lib, over=over), "tests/https.om")
                https_integ(omc, mbin)
            except Fail as e:
                raise Fail("https mutant %r does not compile (invalid mutant): %s" % (name, e))
            bad = https_rows(mbin, pki, port, lambda s: None, "mutant")
            if bad is None:
                raise Fail("https mutant %r SURVIVED every row" % name)
            log("https mutant killed: %s (%s.om line %d, by row %r)" % (name, unit, ln, bad))
        log("HTTPS-MUTANTS PASS killed=%d/%d" % (len(HTTPS_MUTANTS), len(HTTPS_MUTANTS)))
        if os.environ.get("TLS_NET") == "1":
            for host, path, needle in (("www.example.com", "/", b"Example Domain"), ("api.github.com", "/licenses/mit", b'"key":"mit"')):
                rc, body, st, err, det = https_run(hbin, host, 443, "-", path, connect=host, timeout=60)
                ok = rc == 0 and st == 200 and needle in body
                log("https live %s%s rc=%s status=%s err=%s detail=%s body=%d sha256=%s -> %s" % (host, path, rc, st, err, det, len(body), sha(body), "ok" if ok else "MISMATCH"))
                if not ok:
                    raise Fail("https live GET %s%s" % (host, path))
            log("HTTPS-LIVE PASS rows=2")
        else:
            log("HTTPS-LIVE NOT_RUN (set TLS_NET=1 for two live read-only GETs)")
    finally:
        stop.set()
        for proc in osrv:
            proc.kill()
            proc.wait()
    log("HTTPS PASS")


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
    http_src = open(LIB, "rb").read()
    notls_src = open(NOTLS, "rb").read()
    lib_src = http_src + notls_src
    work = keep or tempfile.mkdtemp(prefix="http-check-")
    os.makedirs(work, exist_ok=True)
    out = []

    def log(s):
        out.append(s)
        print(s, flush=True)

    log("omc sha256=%s http.om sha256=%s notls.om sha256=%s" % (sha(open(omc, "rb").read()), sha(http_src), sha(notls_src)))
    status = 0
    try:
        cpath, expected = stage_corpus(omc, work, lib_src, log)
        stage_mutants(omc, work, lib_src, cpath, expected, log)
        stage_live(omc, work, lib_src, log)
        stage_live_mutants(omc, work, lib_src, log)
        stage_e2e(omc, work, lib_src, log)
        stage_https(omc, work, http_src, notls_src, log)
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
