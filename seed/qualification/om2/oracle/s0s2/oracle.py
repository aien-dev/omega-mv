# SPDX-License-Identifier: Apache-2.0
# Independent oracle for seed/qualification/om2/sec/s0s2 (L836 Phase B, lane FixLex).
# It never runs a compiler. It computes expected stdout, stderr and formatter text from the
# contract (spec/omegese-om2.md sha256 245bb74e..0881, errata E1/E2/E4) and from the fixture bytes.
# Run from the worktree root:  python3 seed/qualification/om2/oracle/s0s2/oracle.py
import os

ROOT = "seed/qualification/om2/sec/s0s2"
EXP = ROOT + "/exp"
M64 = (1 << 64) - 1


def rd(rel):
    with open(os.path.join(ROOT, rel), "rb") as f:
        return f.read()


def wr(name, data):
    if isinstance(data, str):
        data = data.encode()
    with open(os.path.join(EXP, name), "wb") as f:
        f.write(data)


def lines_out(vals):
    return "".join("%d\n" % v for v in vals)


# ---------------------------------------------------------------- opt-in line (contract 0, line 13)
def first_non_note_line(src):
    """1-based number of the first line whose first word is not `note` (a blank line counts)."""
    for i, ln in enumerate(src.split(b"\n"), 1):
        words = ln.split()
        if not (ln.startswith(b"note") and words and words[0] == b"note"):
            return i
    raise ValueError("no non-note line")


def human(line, code):
    return "omc: line %d: error %d\n" % (line, code)


def line_of(src, needle, nth=1):
    """1-based line of the nth non-note line containing needle as a whole space/tab-separated word."""
    seen = 0
    for i, ln in enumerate(src.split(b"\n"), 1):
        words = ln.split()
        if words and words[0] != b"note" and needle in words:
            seen += 1
            if seen == nth:
                return i
    raise ValueError(needle)


# ---------------------------------------------------------------- 0: OM-1 names keep their meaning
def names_fn():
    # Re-implementation of the user functions in s0/names-fn.om (OMEGESE 9.3: unsigned wraps).
    u32 = 0xFFFFFFFF
    rotr = lambda x, n: (x ^ n) & u32
    rotl = lambda x, n: (x - n) & u32
    return [rotr(5, 3), rotl(4000000000, 7), (14 * 3) & M64, 20 + 22, sum(range(10)), 1 + 100,
            15 ^ 255, 1001 >> 1, 100 % 7, (0 - 1) & M64, (4294967296 * 4294967296) & M64,
            1234 & 15, 1 | 256, (3 << 4) & M64, 7]


def names_var():
    fresh, at, unroll, inline, seal, opn = 3, 4, 5, 6, 7, 8
    a = [fresh + at, unroll * inline, seal - 1, opn << 2]
    layout = [9] * 4
    field = [11] * 2
    layout[1] = 42
    field[0] = field[1] + 1
    c_at, c_open, c_part = 1, 2, [3, 3]   # one value per field; array field gets it in every element
    c_at = 30
    c_part[1] = c_open * 5
    rotl = 9 + 1
    fmt = 12 + 13
    return a + [layout[1], layout[3], field[0], c_at, c_part[0], c_part[1], rotl, fmt]


# ---------------------------------------------------------------- 2: hex literal values
def hexv(t):
    assert t.startswith("0x") and 1 <= len(t) - 2 <= 16
    assert all(c in "0123456789abcdef" for c in t[2:])
    return int(t[2:], 16)


def hex_max():
    lits = ["0xff", "0xffff", "0xffffffff", "0xffffffffffffffff", "0x7f", "0x7fff", "0x7fffffff",
            "0x7fffffffffffffff", "0x0", "0x00000000000000ff", "0x0123456789abcdef"]
    want = [2**8 - 1, 2**16 - 1, 2**32 - 1, 2**64 - 1, 2**7 - 1, 2**15 - 1, 2**31 - 1, 2**63 - 1,
            0, 255, 81985529216486895]
    vals = [hexv(t) for t in lits]
    assert vals == want, (vals, want)
    return vals


def hex_places():
    a = [hexv("0x0")] * hexv("0x4")
    a[hexv("0x1")] = hexv("0x68")
    a[hexv("0x3")] = hexv("0xa")
    s = sum(range(hexv("0x5")))
    out = [s, 2 * hexv("0x15"), a[1]]
    if s == hexv("0xa"):
        out.append(hexv("0xabc"))
    y = hexv("0x10") - hexv("0x20")
    if y < 0:
        out.append(-y)
    return lines_out(out) + "hex\n", a[3]


# ---------------------------------------------------------------- 2: canonical formatter (E4)
WS = (b" ", b"\t")


def is_note(ln):
    """E6 (c,d): a note line is any line whose first token is `note`, indented or not."""
    w = ln.split()
    return bool(w) and w[0] == b"note"


def fmt_line(ln):
    if is_note(ln):
        return ln                                   # kept byte for byte (E6 c,d; contract line 67)
    if not ln.strip(b" \t"):
        return b""                                  # whitespace-only line becomes empty (E6 b)
    i = 0
    while i < len(ln) and ln[i:i + 1] in WS:
        i += 1
    indent, rest = ln[:i], ln[i:]
    toks, gaps, j = [], [], 0                       # gaps[k] = whitespace written before toks[k]
    while j < len(rest):
        g = j
        while j < len(rest) and rest[j:j + 1] in WS:
            j += 1
        if j == len(rest):
            break                                   # trailing whitespace: dropped (line 65)
        k = j
        if rest[j:j + 1] == b'"':                   # text literal: bytes up to the closing quote
            k += 1
            while rest[k:k + 1] != b'"':
                k += 2 if rest[k:k + 1] == b"\\" else 1
                if k > len(rest):
                    raise ValueError("open text literal")
            k += 1
        else:
            while k < len(rest) and rest[k:k + 1] not in WS:
                k += 1
        gaps.append(rest[g:j])
        toks.append(rest[j:k])
        j = k
    out = indent + toks[0]
    for k in range(1, len(toks)):
        near_paren = toks[k - 1][-1:] in (b"(", b")") or toks[k][:1] in (b"(", b")")
        out += (gaps[k] if near_paren else b" ") + toks[k]   # E6 (e): paren spacing kept as written
    return out


def fmt(rest):
    assert b"\r" not in rest                        # E6 (g): CR is error 6 anywhere
    body = rest[:-1] if rest.endswith(b"\n") else rest
    last = body.split(b"\n")[-1]
    assert last.strip(b" \t"), "trailing blank lines are error 6 (E6 a), not a fmt output case"
    out = b"\n".join(fmt_line(ln) for ln in body.split(b"\n")) + b"\n"
    assert out.count(b"\n") == body.count(b"\n") + 1    # line count unchanged (line 72)
    return out


# ---------------------------------------------------------------- 1: omc-diag records (E4)
def span_of(src, line, tok, nth=1):
    """Byte span [A,B) of the nth whole token `tok` on 1-based `line`, and its 1-based column."""
    starts = [0]
    for i, b in enumerate(src):
        if b == 0x0A:
            starts.append(i + 1)
    ls = starts[line - 1]
    le = src.index(b"\n", ls) if b"\n" in src[ls:] else len(src)
    text = src[ls:le]
    pos, seen = 0, 0
    while True:
        k = text.index(tok, pos)
        before = text[k - 1:k] if k else b" "
        after = text[k + len(tok):k + len(tok) + 1] or b" "
        if before in (b" ", b"\t") and after in (b" ", b"\t"):
            seen += 1
            if seen == nth:
                return ls + k, ls + k + len(tok), k + 1
        pos = k + 1


def eof_span(src):
    a = len(src)
    ls = src.rfind(b"\n") + 1
    line = src.count(b"\n") + 1
    return a, a, a - ls + 1, line


def rec(code, sev, line, col=None, a=None, b=None, expect=None, found=None, obligation="-"):
    f = ["omc-diag", "1", "code=%d" % code, "sev=%s" % sev, "line=%d" % line]
    if col is not None:
        f.append("col=%d" % col)
        f.append("span=%d-%d" % (a, b))
    if expect is not None:
        f.append("expect=%s" % expect)
    if found is not None:
        f.append("found=%s" % found)
    f += ["obligation=%s" % obligation, "profile=OM-2"]
    return " ".join(f) + "\n"


def found_of(src, a, b):
    return src[a:b][:32].hex() if b > a else "-"


def diag():
    out = {}
    # d-left: tokens left; offending token = first extra token "3"; parser wanted end of line.
    s = rd("s1/d-left.om"); ln = line_of(s, b"ret")
    a, b, col = span_of(s, ln, b"3")
    out["d-left.err"] = human(ln, 5) + rec(5, "error", ln, col, a, b, "newline", found_of(s, a, b))
    s = rd("s1/d-left-plain.om"); out["d-left-plain.err"] = human(line_of(s, b"ret"), 5)
    # d-eof-left: same, input ends after the extra token -> still sev=error (no continuation fixes it)
    s = rd("s1/d-eof-left.om"); ln = line_of(s, b"ret")
    a, b, col = span_of(s, ln, b"3")
    out["d-eof-left.err"] = human(ln, 5) + rec(5, "error", ln, col, a, b, "newline", found_of(s, a, b))
    # d-eof-mid: input ends inside `ret add x`; a continuation (` 2\nend\n`) is accepted.
    s = rd("s1/d-eof-mid.om"); a, b, col, ln = eof_span(s)
    out["d-eof-mid.err"] = human(ln, 4) + rec(4, "incomplete", ln, col, a, b, "value", "-")
    # d-eof-noend: the function has no end; EOF after the final LF: line = last line + 1, col 1;
    # E6 Q2: expect=end, sev=incomplete.
    s = rd("s1/d-eof-noend.om"); a, b, col, ln = eof_span(s)
    assert col == 1
    out["d-eof-noend.err"] = human(ln, 15) + rec(15, "incomplete", ln, col, a, b, "end", "-")
    # d-mid-short: E6 Q2: the LF ending `ret add x` is the offending token, sev=error. OM-1 reports
    # the human line one later (the end line). The record's line field (LF's line or the human N)
    # and expect are not fixed by E4/E6, so the tok line omits them; col is A's column on A's line.
    s = rd("s1/d-mid-short.om"); lret = line_of(s, b"ret")
    starts = [0] + [i + 1 for i, c in enumerate(s) if c == 0x0A]
    a = s.index(b"\n", starts[lret - 1]); b = a + 1
    out["d-mid-short.tok"] = (human(lret + 1, 4) + "omc-diag 1 code=4 sev=error col=%d span=%d-%d "
                              "found=%s obligation=- profile=OM-2\n"
                              % (a - starts[lret - 1] + 1, a, b, found_of(s, a, b)))
    assert found_of(s, a, b) == "0a"
    # E6 Q2: bad or out-of-range literal -> expect=number.
    for n, code, tok in (("d-hexupper", 6, b"0xFF"), ("d-hexrange", 8, b"0xffffffff"),
                         ("d-longtok", 6, b"0x0000000000000000000000000000000000000001")):
        s = rd("s1/%s.om" % n); ln = line_of(s, tok)
        a, b, col = span_of(s, ln, tok)
        out[n + ".err"] = human(ln, code) + rec(code, "error", ln, col, a, b, "number", found_of(s, a, b))
    s = rd("s1/d-hexupper-plain.om"); out["d-hexupper-plain.err"] = human(line_of(s, b"0xFF"), 6)
    # E6 Q2: unknown name -> expect=name; incomplete call to an unread function -> callee span.
    for n, code, tok in (("d-unknown-fn", 3, b"foo"), ("d-unknown-struct", 16, b"Pt"),
                         ("d-unknown-sparam", 16, b"Pt"), ("d-unknown-args", 3, b"g")):
        s = rd("s1/%s.om" % n); ln = line_of(s, tok)
        a, b, col = span_of(s, ln, tok)
        out[n + ".err"] = human(ln, code) + rec(code, "unknown", ln, col, a, b, "name", found_of(s, a, b))
    assert len(bytes.fromhex(out["d-longtok.err"].split("found=")[1].split()[0])) == 32
    return out


def text_bytes(lit):
    """Bytes of a text literal per OMEGESE.md 9.4: \\n 10, \\t 9, \\\\ 92, \\" 34."""
    assert lit[:1] == b'"' and lit[-1:] == b'"'
    body, out, i = lit[1:-1], bytearray(), 0
    esc = {b"n": 10, b"t": 9, b"\\": 92, b'"': 34}
    while i < len(body):
        if body[i:i + 1] == b"\\":
            out.append(esc[body[i + 1:i + 2]])
            i += 2
        else:
            out.append(body[i])
            i += 1
    return bytes(out)


def text_of(rel, name):
    """Bytes of the text literal `name` declared in fixture rel (any spacing between the words)."""
    ln = [l for l in rd(rel).split(b"\n") if l.split()[:2] == [b"text", name]][0]
    return text_bytes(ln[ln.index(b'"'):ln.rindex(b'"') + 1])


def x1_run():
    # s2fmt/x1.om writes text msg to stdout and returns twice 0x15 (exit status, 9.1).
    return text_of("s2fmt/x1.om", b"msg"), 2 * hexv("0x15")


def x5_run():
    # s2fmt/x5.om (OM-1) writes text m to stdout and returns 9.
    return text_of("s2fmt/x5.om", b"m"), 9


def main():
    os.makedirs(EXP, exist_ok=True)
    wr("names-fn.out", lines_out(names_fn()))
    wr("names-var.out", lines_out(names_var()))
    wr("hex-max.out", lines_out(hex_max()))
    o, code = hex_places()
    wr("hex-places.out", o)
    assert code == 10
    wr("om1-crlf.out", "42\n")
    hdr = rd("s2fmt/hdr.txt")
    assert hdr == b"om2 fmt\n"
    for x in ("x1", "x2", "x3", "x4", "x5"):
        src = rd("s2fmt/%s.om" % x)
        e = fmt(src)
        assert fmt(e) == e                           # idempotent (line 69)
        wr("%s.fmt" % x, e)
    assert rd("s2fmt/x2.om") == open(os.path.join(EXP, "x2.fmt"), "rb").read()
    o, code = x1_run()
    assert code == 42
    wr("x1.out", o)
    o, code = x5_run()
    assert o == b"ok  \n" and code == 9
    wr("x5.out", o)
    for k, v in diag().items():
        wr(k, v)


if __name__ == "__main__":
    main()
