#!/usr/bin/env python3
"""Deterministic corpus and expected outputs for seed/json (omega-mv #788).

Read-only reference oracle and test harness, never part of the build:
  * CPython json with its C scanner (json.loads, strict) and lexeme-preserving hooks
    (parse_int / parse_float receive the exact number text, object_pairs_hook keeps member
    order and duplicates, parse_constant refuses NaN / Infinity);
  * CPython float() / repr() / struct for binary64 conversion and shortest text;
  * json.encoder.c_encode_basestring for writer escaping.
RFC 8259 rules the oracle adds on top of CPython (documented in README.md): an input
CPython accepts is still refused when a string decodes to a lone surrogate (section 8.2),
when nesting exceeds the depth limit, when the input exceeds the size limit, or when the
token count exceeds the driver's tape.

Usage: gen.py OUTDIR. Writes OUTDIR/cases.tsv (id, mode, arg2, arg3, kind) and per case
OUTDIR/<id>.in and OUTDIR/<id>.<mode>.want. kind `exact` compares bytes; kind `err` only
requires the driver to print a line starting `ERR ` (the parse was refused).
"""
import json
import json.encoder
import json.scanner
import os
import random
import struct
import sys
from decimal import Decimal, getcontext

sys.setrecursionlimit(20000)
sys.set_int_max_str_digits(0)
getcontext().prec = 4000
assert json.scanner.c_make_scanner is not None, "CPython C scanner required"
assert json.encoder.c_encode_basestring is not None, "CPython C encoder required"

MAXN = 983040                    # js_init size limit cap
DLIM = 512                       # driver default depth limit
TAPE = 262144                    # driver tape cells (jsdrv.om dvtape)
CAP = (TAPE - 16) // 4           # tokens the driver tape holds
DRVSRC = 524288                  # driver input array (jsdrv.om dvsrc)
WCAP = 1048576                   # driver writer array (jsdrv.om dvob)
GATEWAY = 300 * 1024             # gateway request body size


class Num(str):
    """A number lexeme, exactly as written."""


class Obj:
    def __init__(self, pairs):
        self.pairs = pairs


class Reject(Exception):
    pass


def _const(s):
    raise Reject(s)


def stream(v, out, d):
    """Token lines in tape order; returns the maximum number of open containers."""
    if isinstance(v, Obj):
        out.append("{%d" % len(v.pairs))
        m = d + 1
        for k, x in v.pairs:
            out.append("K " + k.encode("utf-8").hex())
            m = max(m, stream(x, out, d + 1))
        out.append("}")
        return m
    if isinstance(v, list):
        out.append("[%d" % len(v))
        m = d + 1
        for x in v:
            m = max(m, stream(x, out, d + 1))
        out.append("]")
        return m
    if isinstance(v, Num):
        out.append("N " + v)
        return d
    if isinstance(v, str):
        out.append("S " + v.encode("utf-8").hex())
        return d
    if v is True:
        out.append("T")
    elif v is False:
        out.append("F")
    elif v is None:
        out.append("Z")
    else:
        raise AssertionError(type(v))
    return d


def oracle(b, slim=MAXN, dlim=DLIM):
    """(value, lines, depth) when the input must be accepted, else None."""
    if len(b) > slim:
        return None
    try:
        s = b.decode("utf-8")
    except UnicodeDecodeError:
        return None
    try:
        v = json.loads(s, parse_int=Num, parse_float=Num,
                       parse_constant=_const, object_pairs_hook=Obj)
    except (ValueError, Reject, RecursionError):
        return None
    lines = []
    try:
        depth = stream(v, lines, 0)
    except UnicodeEncodeError:
        return None
    if depth > dlim or len(lines) > CAP:
        return None
    return v, lines, depth


def canon(v, out):
    if isinstance(v, Obj):
        out.append("{")
        for i, (k, x) in enumerate(v.pairs):
            if i:
                out.append(",")
            out.append(json.encoder.c_encode_basestring(k))
            out.append(":")
            canon(x, out)
        out.append("}")
    elif isinstance(v, list):
        out.append("[")
        for i, x in enumerate(v):
            if i:
                out.append(",")
            canon(x, out)
        out.append("]")
    elif isinstance(v, Num):
        out.append(str(v))
    elif isinstance(v, str):
        out.append(json.encoder.c_encode_basestring(v))
    elif v is True:
        out.append("true")
    elif v is False:
        out.append("false")
    else:
        out.append("null")


def nums(v, out):
    if isinstance(v, Obj):
        for _, x in v.pairs:
            nums(x, out)
    elif isinstance(v, list):
        for x in v:
            nums(x, out)
    elif isinstance(v, Num):
        out.append(str(v))


def fline(lex):
    x = float(lex)
    bits = struct.pack(">d", x).hex()
    if any(c in lex for c in ".eE"):
        return "%s %s 0 -" % (repr(x), bits)
    i = int(lex)
    if abs(i) > 2**64 - 1:
        return "%s %s 2 -" % (repr(x), bits)
    if lex.startswith("-") and i < -2**63:
        return "%s %s 1 -" % (repr(x), bits)
    return "%s %s 1 %d" % (repr(x), bits, i)


class Corpus:
    def __init__(self, outdir):
        self.dir = outdir
        self.rows = []
        self.n = 0
        os.makedirs(outdir, exist_ok=True)

    def _new(self, data):
        self.n += 1
        cid = "c%05d" % self.n
        with open(os.path.join(self.dir, cid + ".in"), "wb") as f:
            f.write(data)
        return cid

    def _want(self, cid, mode, data):
        with open(os.path.join(self.dir, "%s.%s.want" % (cid, mode)), "wb") as f:
            f.write(data)

    def parse(self, data, slim=None, dlim=None):
        """All four parse modes, expected from the oracle."""
        s = MAXN if slim is None else slim
        d = DLIM if dlim is None else dlim
        a2 = "-" if slim is None else str(slim)
        a3 = "-" if dlim is None else str(dlim)
        if slim is None and dlim is not None:
            a2 = str(MAXN)
        cid = self._new(data)
        r = oracle(data, s, d)
        if r is None:
            for mode in "twfr":
                self._want(cid, mode, b"ERR\n")
                self.rows.append((cid, mode, a2, a3, "err"))
            return cid, False
        v, lines, depth = r
        toks = ("\n".join(lines) + "\nOK\n").encode()
        self._want(cid, "t", toks)
        self._want(cid, "w", toks)
        nl = []
        nums(v, nl)
        self._want(cid, "f", "".join(fline(x) + "\n" for x in nl).encode() + b"OK\n")
        if depth > 1024:
            self._want(cid, "r", b"WERR 25 0\n")
        else:
            out = []
            canon(v, out)
            self._want(cid, "r", "".join(out).encode("utf-8"))
        for mode in "twfr":
            self.rows.append((cid, mode, a2, a3, "exact"))
        return cid, True

    def pinned(self, data, want, slim=None, dlim=None):
        """A refusal whose exact code and position are part of the contract (README)."""
        s = MAXN if slim is None else slim
        d = DLIM if dlim is None else dlim
        assert oracle(data, s, d) is None, ("oracle accepts pinned refusal", data[:40])
        a2 = str(s)
        a3 = "-" if dlim is None else str(dlim)
        cid = self._new(data)
        for mode in "twfr":
            self._want(cid, mode, want.encode() + b"\n")
            self.rows.append((cid, mode, a2, a3, "exact"))
        return cid

    def raw(self, mode, data, want, a2="-", a3="-"):
        cid = self._new(data)
        self._want(cid, mode, want)
        self.rows.append((cid, mode, a2, a3, "exact"))
        return cid

    def finish(self):
        with open(os.path.join(self.dir, "cases.tsv"), "w") as f:
            for row in self.rows:
                f.write("\t".join(row) + "\n")


# ---------------------------------------------------------------- hand cases

HAND = [
    b"null", b"true", b"false", b"0", b"-0", b"1", b"-1", b"12.5", b"1e5", b"1E+5", b"1e-5",
    b"-0.0e-0", b'""', b'"a"', b"[]", b"{}", b" [ ] ", b"\t\n\r [1,2 ,3]\r\n", b'{"a":1,"a":2}',
    b'{"":""}', b'[[[[]]]]', b'{"a":{"b":{"c":[1,{"d":null}]}}}', b'["\\"\\\\\\/\\b\\f\\n\\r\\t"]',
    b'"\\u0000"', b'"\\u001f"', b'"\\u001F\\u00e9\\u00E9"', b'"\\u2028\\u2029"', b'"\\uD83D\\uDE00"',
    b'"\\ud83d\\ude00"', b'"\\uDBFF\\uDFFF"', b'"\\uD800\\uDC00"', b'"\\uFFFF"', b'"\\uE000"',
    '"é€😀\U0010ffff\ud7ff\ue000"'.encode("utf-8", "surrogatepass"), b'"\x7f"', b'"\xc2\x80"',
    b'"\xdf\xbf"', b'"\xe0\xa0\x80"', b'"\xed\x9f\xbf"', b'"\xee\x80\x80"', b'"\xef\xbf\xbf"',
    b'"\xf0\x90\x80\x80"', b'"\xf4\x8f\xbf\xbf"', b"123456789012345678901234567890",
    b"18446744073709551615", b"18446744073709551616", b"-9223372036854775808",
    b"-9223372036854775809", b"9223372036854775807", b"9223372036854775808",
    b"1.7976931348623157e308", b"1.7976931348623158e308", b"1.7976931348623159e308", b"1e400",
    b"-1e400", b"5e-324", b"2.4703282292062327e-324", b"2.4703282292062328e-324", b"1e-400",
    b"2.2250738585072011e-308", b"2.2250738585072012e-308", b"9007199254740993",
    b"9007199254740993.0000000000000000000000000000001", b"0.1", b"0.30000000000000004",
    b"1e16", b"1e15", b"123456789012345680", b"0.0001", b"0.00001", b"1e999999999",
    b"0e999999999", b"1e-999999999", b"100e-2", b"0.000000000000000000000000000000001e33",
    # refused by the oracle as well
    b"", b" ", b"[", b"]", b"{", b"[1,]", b"[,1]", b"{,}", b'{"a"}', b'{"a":}', b'{"a" 1}',
    b"{1:2}", b"[1 2]", b"01", b"-01", b"00", b"1.", b".5", b"-", b"+1", b"1e", b"1e+", b"1.e5",
    b"0x10", b"Infinity", b"-Infinity", b"NaN", b"nan", b"tru", b"truex", b"nul", b"nulll",
    b"True", b"[True]", b'"abc', b'"\x01"', b'"\x1f"', b'"\t"', b'"\n"', b'"\\x"', b'"\\u12"',
    b'"\\u12G4"', b'"\\U0041"', b'"\\ud800"', b'"\\udc00"', b'"\\ud800\\u0041"', b'"\\ud800x"',
    b'"\\udbff\\udbff"', b'"\\ud800\\"', b'"\xc0\xaf"', b'"\xc1\xbf"', b'"\xe0\x80\x80"',
    b'"\xe0\x9f\xbf"', b'"\xed\xa0\x80"', b'"\xed\xbf\xbf"', b'"\xf0\x80\x80\x80"',
    b'"\xf0\x8f\xbf\xbf"', b'"\xf4\x90\x80\x80"', b'"\xf5\x80\x80\x80"', b'"\xff"', b'"\x80"',
    b'"\xe2\x82"', b'"\xf0\x9f\x98"', b'"\xc2"', b"\xef\xbb\xbf{}", b"{} x", b"[] []",
    b"[1]\x00", b"\x00", b"[1,2", b'{"a":1', b'["a\\', b"[\x0c]", b"[\xa0]", b"'a'",
    b'{"a":1,}', b"[-]", b"[1.5e]", b"[0.e1]", b"/* */ 1", b"[1]//",
]

PINNED = [
    (b"", "ERR 1 0", None, None),
    (b"   ", "ERR 1 3", None, None),
    (b"[", "ERR 1 1", None, None),
    (b"[1,]", "ERR 2 3", None, None),
    (b'{"a":1,}', "ERR 2 7", None, None),
    (b"[1 2]", "ERR 2 3", None, None),
    (b'{"a" 1}', "ERR 2 5", None, None),
    (b"{1:2}", "ERR 2 1", None, None),
    (b"+1", "ERR 2 0", None, None),
    (b".5", "ERR 2 0", None, None),
    (b"NaN", "ERR 2 0", None, None),
    (b"\xef\xbb\xbf{}", "ERR 2 0", None, None),
    (b"{} x", "ERR 3 3", None, None),
    (b"[[[]]]", "ERR 4 2", None, 2),
    (b"[1,2,3,4,5]", "ERR 5 10", 10, None),
    (b'"\xc0\xaf"', "ERR 6 1", None, None),
    (b'"\xed\xa0\x80"', "ERR 6 2", None, None),
    (b'"\xf4\x90\x80\x80"', "ERR 6 2", None, None),
    (b'"\xe2\x82"', "ERR 6 3", None, None),
    (b'"\\udc00"', "ERR 7 1", None, None),
    (b'"\\ud800"', "ERR 7 1", None, None),
    (b'"\\ud800\\u0041"', "ERR 7 7", None, None),
    (b'"\x01"', "ERR 8 1", None, None),
    (b'"\t"', "ERR 8 1", None, None),
    (b'"\\x"', "ERR 9 2", None, None),
    (b'"\\u12"', "ERR 9 5", None, None),
    (b"01", "ERR 10 1", None, None),
    (b"1.", "ERR 10 2", None, None),
    (b"-", "ERR 10 1", None, None),
    (b"1e+", "ERR 10 3", None, None),
    (b"[-]", "ERR 10 2", None, None),
    (b"tru", "ERR 1 3", None, None),
    (b"nul", "ERR 1 3", None, None),
    (b"trux", "ERR 12 3", None, None),
    (b'"abc', "ERR 1 4", None, None),
    (b'["a\\', "ERR 1 4", None, None),
]

FLOATS = [
    "0", "-0", "0.0", "-0.0", "1", "-1", "0.1", "0.2", "0.3", "0.30000000000000004", "1e23",
    "8.98846567431158e307", "1.7976931348623157e308", "1.7976931348623158e308",
    "1.7976931348623159e308", "4.9406564584124654e-324", "2.4703282292062328e-324",
    "2.4703282292062327e-324", "2.2250738585072011e-308", "2.2250738585072014e-308",
    "2.225073858507201e-308", "9007199254740992", "9007199254740993", "9007199254740995",
    "9007199254740994.9999999999999999999999999999999999", "123456789012345678901234567890",
    "3.141592653589793238462643383279502884197169399375105820974944592307816406286",
    "1e22", "1e21", "1e-7", "0.000123", "1234567890123456.7", "12345678901234567",
    "4.35679015789893e-307", "7.3177701707893310e+15", "5e-324", "1e308", "1e-308",
]


def doubles(R, n):
    out = []
    while len(out) < n:
        k = R.randrange(6)
        if k == 0:
            bits = R.getrandbits(64)
        elif k == 1:
            bits = R.getrandbits(52) | (R.randrange(1, 60) << 52)          # subnormal-adjacent
        elif k == 2:
            bits = R.getrandbits(1) << 63 | R.getrandbits(52)              # subnormal
        elif k == 3:
            bits = R.randrange(1, 2047) << 52                              # powers of two
        elif k == 4:
            bits = (R.randrange(990, 1100) << 52) | R.getrandbits(52)       # near 1
        else:
            bits = (R.randrange(2000, 2047) << 52) | R.getrandbits(52)      # huge
        if (bits >> 52) & 2047 == 2047:
            continue
        out.append(struct.unpack(">d", struct.pack(">Q", bits))[0])
    return out


def nextup(x):
    b = struct.unpack(">Q", struct.pack(">d", x))[0]
    return struct.unpack(">d", struct.pack(">Q", b + 1))[0]


def dlex(d):
    s = format(d, "e")
    return s.replace("E", "e")


def float_lexemes(R):
    out = list(FLOATS)
    for x in doubles(R, 400):
        out.append(repr(x))
        out.append("%.25e" % x)
        out.append("%.17g" % x if "e" in "%.17g" % x or "." in "%.17g" % x else "%.17g.0" % x)
    for x in doubles(R, 160):
        x = abs(x)
        y = nextup(x)
        if y == float("inf"):
            continue
        mid = (Decimal(x) + Decimal(y)) / 2
        tiny = Decimal(x if x else y) * Decimal("1e-900")
        out.append(dlex(mid))
        out.append(dlex(mid + tiny))
        out.append(dlex(mid - tiny))
        out.append("-" + dlex(mid))
    for lex in list(out):
        assert oracle(lex.encode()) is not None, lex
    return out


# ---------------------------------------------------------------- random documents

CHARS = [chr(c) for c in range(0x20, 0x7f)] * 3 + [
    '"', "\\", "/", "\x00", "\x01", "\x08", "\x09", "\x0a", "\x0c", "\x0d", "\x1f", "\x7f",
    "\x80", "é", "ß", "€", "\u2028", "\u2029", "\ufeff", "\uffff", "\ud7ff", "\ue000",
    "😀", "\U00010000", "\U0010ffff", "中", "\u0300",
]
SHORT = {'"': '\\"', "\\": "\\\\", "\b": "\\b", "\f": "\\f", "\n": "\\n", "\r": "\\r",
         "\t": "\\t", "/": "\\/"}


def uesc(R, cp):
    h = "%04x" % cp
    return "\\u" + (h.upper() if R.random() < 0.5 else h)


def enc_char(R, ch):
    cp = ord(ch)
    must = ch in '"\\' or cp < 0x20
    r = R.random()
    if must or r < 0.15:
        if ch in SHORT and R.random() < 0.6:
            return SHORT[ch]
        if cp > 0xFFFF:
            v = cp - 0x10000
            return uesc(R, 0xD800 + (v >> 10)) + uesc(R, 0xDC00 + (v & 0x3FF))
        return uesc(R, cp)
    return ch


def rstr(R):
    s = "".join(R.choice(CHARS) for _ in range(R.randint(0, 10)))
    return '"' + "".join(enc_char(R, c) for c in s) + '"'


def rnum(R):
    s = "-" if R.random() < 0.3 else ""
    if R.random() < 0.2:
        s += "0"
    else:
        s += str(R.randint(1, 9)) + "".join(R.choice("0123456789")
                                            for _ in range(R.choice([0, 1, 2, 5, 15, 19, 20, 25, 60])))
    if R.random() < 0.4:
        s += "." + "".join(R.choice("0123456789") for _ in range(R.randint(1, 20)))
    if R.random() < 0.3:
        s += R.choice("eE") + R.choice(["", "+", "-"]) + str(R.randint(0, 400))
    return s


def ws(R):
    if R.random() < 0.7:
        return ""
    return "".join(R.choice(" \t\n\r") for _ in range(R.randint(1, 3)))


def rval(R, d):
    k = R.randrange(10 if d < 6 else 6)
    if k == 0:
        return R.choice(["true", "false", "null"])
    if k in (1, 2):
        return rnum(R)
    if k in (3, 4, 5):
        return rstr(R)
    if k in (6, 7):
        items = [ws(R) + rval(R, d + 1) + ws(R) for _ in range(R.randint(0, 5))]
        return "[" + ",".join(items) + "]" if items else "[" + ws(R) + "]"
    items = [ws(R) + rstr(R) + ws(R) + ":" + ws(R) + rval(R, d + 1) + ws(R)
             for _ in range(R.randint(0, 5))]
    return "{" + ",".join(items) + "}" if items else "{" + ws(R) + "}"


INTEREST = list(b'{}[]:,"\\/ \t\n\r0123456789-+.eEtfnulasu') + [
    0x00, 0x1F, 0x7F, 0x80, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF, 0xE0, 0xED, 0xEF, 0xF0, 0xF4, 0xF5, 0xFF]


def mutate(R, b):
    b = bytearray(b)
    for _ in range(R.randint(1, 3)):
        op = R.randrange(5)
        if not b:
            b.append(R.choice(INTEREST))
            continue
        i = R.randrange(len(b))
        if op == 0:
            b[i] = R.choice(INTEREST)
        elif op == 1:
            del b[i]
        elif op == 2:
            b.insert(i, R.choice(INTEREST))
        elif op == 3:
            del b[i:]
        else:
            j = R.randrange(len(b))
            b[i:i] = b[j:j + R.randint(1, 4)]
    return bytes(b)


def surrogate_doc(R):
    parts = []
    for _ in range(R.randint(1, 4)):
        k = R.randrange(4)
        if k == 0:
            parts.append(uesc(R, R.randrange(0xD800, 0xDC00)))
        elif k == 1:
            parts.append(uesc(R, R.randrange(0xDC00, 0xE000)))
        elif k == 2:
            parts.append(uesc(R, R.randrange(0xD800, 0xDC00)) + uesc(R, R.randrange(0xDC00, 0xE000)))
        else:
            parts.append(R.choice(["a", "\\n", "\\u0041", "é"]))
    return ('["' + "".join(parts) + '"]').encode("utf-8")


def gateway(R):
    head = '{"model":"omega-test","messages":[{"role":"system","content":"You are helpful."},' \
           '{"role":"user","content":"'
    tail = '"}],"max_tokens":1024,"temperature":0.7,"stream":false}'
    pool = list("abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,;:!?") * 8 + \
        ["é", "€", "😀", "中", "\\n", "\\t", '\\"', "\\\\", "\\u00e9", "\\ud83d\\ude00", "\\/"]
    body = []
    size = len(head.encode()) + len(tail.encode())
    while True:
        p = R.choice(pool)
        k = len(p.encode())
        if size + k > GATEWAY:
            break
        body.append(p)
        size += k
    body.append("x" * (GATEWAY - size))
    data = (head + "".join(body) + tail).encode()
    assert len(data) == GATEWAY
    return data


# ---------------------------------------------------------------- writer scripts

def wscript(R, v, lines):
    if isinstance(v, Obj):
        lines.append("o")
        for k, x in v.pairs:
            wstr(R, k, lines)
            wscript(R, x, lines)
        lines.append("O")
    elif isinstance(v, list):
        lines.append("a")
        for x in v:
            wscript(R, x, lines)
        lines.append("A")
    elif isinstance(v, Num):
        lines.append("n" + v)
    elif isinstance(v, str):
        wstr(R, v, lines)
    elif v is True:
        lines.append("t")
    elif v is False:
        lines.append("f")
    else:
        lines.append("z")


def wstr(R, s, lines):
    b = s.encode("utf-8")
    if R.random() < 0.5:
        lines.append("s" + b.hex())
    else:
        lines.append("S")
        lines.extend("b%02x" % c for c in b)
        lines.append("E")


WPINNED = [
    ("z", "OK null"), ("o\nO", "OK {}"), ("a\nA", "OK []"),
    ("o\ns61\nu1\ns62\na\nt\nf\nz\nA\nO", 'OK {"a":1,"b":[true,false,null]}'),
    ("s" + b'"\\/\x08\x0c\n\r\t\x00\x1f\x7f'.hex(),
     'OK "\\"\\\\/\\b\\f\\n\\r\\t\\u0000\\u001f\x7f"'),
    ("l-9223372036854775808", "OK -9223372036854775808"), ("l-1", "OK -1"), ("l0", "OK 0"),
    ("u18446744073709551615", "OK 18446744073709551615"), ("u0", "OK 0"),
    ("n-0.0e+00", "OK -0.0e+00"), ("a\nn1\nn-2.5E-3\nA", "OK [1,-2.5E-3]"),
    ("n01", "ERR 24 0"), ("n1.", "ERR 24 0"), ("n", "ERR 24 0"), ("n1x", "ERR 24 0"),
    ("n+1", "ERR 24 0"), ("d7ff8000000000000", "ERR 24 0"), ("d7ff0000000000000", "ERR 24 0"),
    ("dfff0000000000000", "ERR 24 0"), ("z\nz", "ERR 22 0"), ("o\ns61\nO", "ERR 22 0"),
    ("a\nO", "ERR 22 0"), ("O", "ERR 22 0"), ("a", "ERR 22 0"), ("", "ERR 22 0"),
    ("o\nu1", "ERR 22 0"), ("S\nz", "ERR 22 0"), ("b41", "ERR 22 0"), ("E", "ERR 22 0"),
    ("sc0af", "ERR 23 0"), ("seda080", "ERR 23 0"), ("sf4908080", "ERR 23 0"),
    ("S\nbe2\nE", "ERR 23 0"), ("S\nb80\nE", "ERR 23 0"), ("S\nb41\nE", 'OK "A"'),
    ("o\nS\nb61\nE\nS\nb62\nE\nO", 'OK {"a":"b"}'),
    ("o\nsf09f9880\nsf09f9880\nO", 'OK {"\U0001F600":"\U0001F600"}'),
    ("se280a8", 'OK "\u2028"'), ("a\nz\nz\nA", "OK [null,null]"),
    ("o\ns\nz\ns\nz\nO", 'OK {"":null,"":null}'),
]


def main():
    out = sys.argv[1]
    R = random.Random(788)
    C = Corpus(out)
    for p in PINNED:
        C.pinned(p[0], p[1], p[2], p[3])
    for b in HAND:
        C.parse(b)
    # limits: size and depth boundaries, token capacity, gateway body
    C.parse(b"[" * DLIM + b"]" * DLIM)
    C.pinned(b"[" * (DLIM + 1) + b"]" * (DLIM + 1), "ERR 4 %d" % DLIM)
    C.parse(b"[" * 1100 + b"]" * 1100, dlim=1100)
    C.parse(b'{"a":' * 700 + b"1" + b"}" * 700, dlim=1024)
    C.parse(b"[1,2,3,4,5]", slim=11)
    C.parse(b"[" + b"0," * (CAP - 3) + b"0]")
    C.pinned(b"[" + b"0," * (CAP - 1) + b"0]", "ERR 11 %d" % (1 + 2 * (CAP - 1)))
    g = gateway(R)
    cid, ok = C.parse(g, slim=GATEWAY)
    assert ok
    C.pinned(g, "ERR 5 %d" % (GATEWAY - 1), slim=GATEWAY - 1)
    C.raw("t", b"1", b"INIT -13\n", str(MAXN + 1), "-")
    C.raw("t", b"1", b"INIT -13\n", str(MAXN), "0")
    C.raw("t", b"1", b"INIT -13\n", str(MAXN), "65536")
    C.raw("t", b"1", b"INIT -13\n", "0", "-")
    C.raw("t", b"[[1]]", b"[1\n[1\nN 1\n]\n]\nOK\n", str(MAXN), "65535")
    # numbers: f64 parity with CPython float(), arrays of 40 lexemes per case
    lex = float_lexemes(R)
    for i in range(0, len(lex), 40):
        C.parse(("[" + ",".join(lex[i:i + 40]) + "]").encode())
    # random valid documents and their mutants
    docs = []
    for _ in range(700):
        d = (ws(R) + rval(R, 0) + ws(R)).encode("utf-8", "surrogatepass")
        docs.append(d)
        C.parse(d)
    for _ in range(900):
        C.parse(mutate(R, R.choice(docs)))
    for _ in range(150):
        C.parse(surrogate_doc(R))
    # writer scripts
    for script, want in WPINNED:
        C.raw("a", script.encode(), want.encode("utf-8") + b"\n")
    C.raw("a", ("a\n" * 1024 + "A\n" * 1024).encode(), b"OK " + b"[" * 1024 + b"]" * 1024 + b"\n")
    C.raw("a", ("a\n" * 1025 + "A\n" * 1025).encode(), b"ERR 25 0\n")
    C.raw("a", b"s68656c6c6f", b"ERR 21 0\n", "1045")
    C.raw("a", b"s68656c6c6f", b'OK "hello"\n', "1047")
    C.raw("a", b"z", b"ERR 26 0\n", "1040")
    for x in doubles(R, 300) + [0.0, -0.0, 1e16, 1e15, 0.0001, 0.00001, 2.0**53, 5e-324, 0.1]:
        bits = struct.pack(">d", x).hex()
        C.raw("a", ("a\nd" + bits + "\nA").encode(), ("OK [" + repr(x) + "]\n").encode())
    for x in [-(2**63), -(2**63) + 1, -1, 0, 1, 2**63 - 1]:
        C.raw("a", ("l%d" % x).encode(), ("OK %d\n" % x).encode())
    for d in docs[:400]:
        r = oracle(d)
        if r is None or r[2] > 1024:
            continue
        lines = []
        wscript(R, r[0], lines)
        o = []
        canon(r[0], o)
        C.raw("a", "\n".join(lines).encode(), b"OK " + "".join(o).encode("utf-8") + b"\n")
    C.finish()
    print("GEN cases=%d rows=%d" % (C.n, len(C.rows)))


if __name__ == "__main__":
    main()
