#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""corpus.py OUTDIR [--check-product DIR]: write the codec parity corpus (deterministic).
corpus.py OUTDIR --tree ROOT: tree.pack and tree.list, every .omega/.osc/.bin file under ROOT/tests
and ROOT/examples (not part of expected.txt: it follows the tree).

Packs (records of u32 little-endian length + bytes):
  decode.pack  wire graphs for omega_graph_deserialize_binary: the product builder vectors, structured
               graphs (every kind, payload shape, sort order, refusal and overread path), single-byte
               mutants and truncations of valid graphs, random byte strings
  text.pack    inputs for omega_graph_parse_text
  hexid.pack   129-byte records: a 65-byte window of a C string for omega_parse_hex_semantic_id, then
               two 32-byte ids for omega_hex_semantic_id / omega_compare_semantic_id

The graphs are built here from the C struct layouts (aarch64 LP64, see README); semantic ids are
sha256 of the canonical encoding, following omega_canonical.c. --check-product DIR compares the
three product-builder vectors with the files the C oracle's `emit` wrote into DIR.
"""
import hashlib
import os
import random
import struct
import sys

KIND_VALUE, KIND_TYPE, KIND_OPERATION, KIND_RELATION, KIND_CONSTRAINT = 1, 2, 3, 4, 5
KIND_MEMORY, KIND_EFFECT = 6, 8


def u16be(v):
    return struct.pack(">H", v & 0xFFFF)


def u32be(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


class Obj:
    def __init__(self, kind, attrs=(), rels=(), consts=(), payload=b"", ver=None, extra=b"", magic=b"OMG0"):
        self.kind = kind
        self.attrs = list(attrs)    # (raw key bytes, value bytes)
        self.rels = list(rels)      # (kind, 32-byte target)
        self.consts = list(consts)  # (kind, payload bytes)
        self.payload = payload
        self.ver = ver
        self.extra = extra
        self.magic = magic

    def wire(self):
        ver = self.ver if self.ver is not None else (2 if self.kind == KIND_EFFECT else 1)
        b = bytearray(self.magic + bytes([ver & 255, self.kind & 255]))
        b += u16be(len(self.attrs))
        for k, v in self.attrs:
            b += bytes([len(k)]) + k + u16be(len(v)) + v
        b += u16be(len(self.rels))
        for k, t in self.rels:
            b += u16be(k) + t
        b += u16be(len(self.consts))
        for k, p in self.consts:
            b += u16be(k) + u16be(len(p)) + p
        b += u32be(len(self.payload)) + self.payload + self.extra
        return bytes(b)

    def canonical(self):
        """omega_canonical_encode of the decoded object (keys end at their first NUL)."""
        kind = self.kind & 255
        b = bytearray(b"OMG0" + bytes([2 if kind == KIND_EFFECT else 1, kind]))
        keys = [(k.split(b"\0")[0], v) for k, v in self.attrs]
        keys = sorted(keys, key=lambda kv: kv[0])
        b += u16be(len(keys))
        for k, v in keys:
            b += bytes([len(k)]) + k + u16be(len(v)) + v
        rels = sorted(self.rels, key=lambda r: (r[0], r[1]))
        b += u16be(len(rels))
        for k, t in rels:
            b += u16be(k) + t
        cs = sorted(self.consts, key=lambda c: (c[0], c[1]))
        b += u16be(len(cs))
        for k, p in cs:
            b += u16be(k) + u16be(len(p)) + p
        b += u32be(len(self.payload)) + self.payload
        return bytes(b)

    def id(self):
        c = self.canonical()
        return hashlib.sha256(c).digest() if len(c) <= 4096 else bytes(32)


def graph(objs, count=None, magic=b"OMGG", tail=b""):
    b = bytearray(magic + u16be(len(objs) if count is None else count))
    for o in objs:
        w = o if isinstance(o, (bytes, bytearray)) else o.wire()
        b += u32be(len(w)) + w
    return bytes(b + tail)


# ---- payload images (aarch64 LP64 layouts of omega_types.h) ----
Z = bytes(32)


def type_pl(tag, width=0, length=0, elem=Z):
    return struct.pack("<IHxxI", tag, width & 0xFFFF, length & 0xFFFFFFFF) + elem


def value_pl(type_id, byte_len, data=b""):
    return type_id + struct.pack("<H", byte_len & 0xFFFF) + (data + bytes(64))[:64]


def op_pl(opcode, overflow, type_id=Z, arity=0, inputs=(Z, Z, Z, Z), output=Z):
    return struct.pack("<II", opcode, overflow) + type_id + bytes([arity]) + b"".join(inputs) + output + bytes(3)


def apply_pl(op_id, n, operands=(Z, Z, Z, Z)):
    return op_id + bytes([n & 255]) + b"".join((list(operands) + [Z] * 4)[:4])


def effect_pl(res, opc, slot, gen, cref=Z, params=b"", plen=None):
    plen = len(params) if plen is None else plen
    return struct.pack(">HHIQ", res, opc, slot, gen) + cref + struct.pack(">H", plen) + (params + bytes(128))[:128]


# ---- product builders (omega_core.c) ----
def b_type(tag, width=0, length=0, elem=Z):
    return Obj(KIND_TYPE, payload=type_pl(tag, width, length, elem))


def b_val_uint(tid, width, val):
    n = min(max((width + 7) // 8, 1), 64)
    data = bytes(((val >> ((n - 1 - i) * 8)) & 255) if (n - 1 - i) * 8 < 64 else 0 for i in range(n))
    return Obj(KIND_VALUE, payload=value_pl(tid, n, data))


def b_op(opc, ov, tid):
    return Obj(KIND_OPERATION, payload=op_pl(opc, ov, tid, 2, (tid, tid, Z, Z), tid))


def b_apply(op, a, b):
    return Obj(KIND_OPERATION, payload=apply_pl(op, 2, (a, b)))


def product_vectors():
    t = b_type(3, 32)
    va, vb = b_val_uint(t.id(), 32, 7), b_val_uint(t.id(), 32, 11)
    op = b_op(3, 1, t.id())
    add = graph([t, va, vb, op, b_apply(op.id(), va.id(), vb.id())])
    law = graph([Obj(KIND_CONSTRAINT, consts=[(6, bytes([0x40, 0, 0, 0, 0, 1, 0, 0]) + bytes(8)),
                                              (9, bytes([0, 0, 0, 7]) + bytes(4))])])
    u32 = b_type(3, 32)
    bt = b_type(2)
    sub = b_op(4, 3, u32.id())
    v1 = b_val_uint(u32.id(), 32, 0xDEADBEEF)
    allb = graph([u32, b_type(4, 16), b_type(5, 8), b_type(7, 0, 10, u32.id()), b_type(1), b_type(2),
                  b_type(14, 32), b_type(6, 8), b_type(11), bt,
                  Obj(KIND_VALUE, payload=value_pl(bt.id(), 1, b"\x01")),
                  Obj(KIND_VALUE, attrs=[(b"omega.param", b"\x00\x03")], payload=value_pl(u32.id(), 0)),
                  sub, v1, b_apply(sub.id(), v1.id(), v1.id()),
                  Obj(KIND_EFFECT, payload=effect_pl(1, 2, 3, 0x0123456789ABCDEF)),
                  Obj(KIND_RELATION, attrs=[(b"a_key", b"2"), (b"z_key", b"1")],
                      rels=[(8, u32.id()), (9, u32.id())])])
    return {"u32_add_graph.bin": add, "physics_m3_law.bin": law, "builders_all.bin": allb}


# ---- structured random graphs ----
def rkey(r):
    c = r.random()
    if c < 0.05:
        return b""
    if c < 0.10:
        return bytes([0]) + r.randbytes(r.randint(0, 5))
    if c < 0.18:
        n = r.randint(1, 8)
        k = bytearray(r.randbytes(n))
        k[r.randrange(n)] = 0
        return bytes(k)
    if c < 0.22:
        return r.randbytes(63)
    if c < 0.40:
        return bytes(r.choice(b"abcz") for _ in range(r.randint(1, 3)))
    if c < 0.50:
        return bytes(r.randint(128, 255) for _ in range(r.randint(1, 4)))
    return bytes(r.choice(b"abcdefghijklmnopqrstuvwxyz._0123456789") for _ in range(r.randint(1, 12)))


def rattrs(r, big=False):
    n = r.choice([0, 0, 1, 2, 3, 5, 8, 32]) if not big else r.randint(4, 32)
    out = []
    for _ in range(n):
        vl = r.choice([0, 1, 2, 7, 64, 512]) if not big else r.randint(100, 512)
        out.append((rkey(r), r.randbytes(vl)))
    if out and r.random() < 0.3:
        out.append(out[0])
    return out[:32]


def rrels(r, ids):
    n = r.choice([0, 0, 0, 1, 2, 3, 6, 64])
    out = []
    for _ in range(n):
        k = r.choice([1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 6, 0, 10, 65535, r.randrange(65536)]) if r.random() < 0.2 else r.randint(1, 9)
        t = r.choice(ids) if ids and r.random() < 0.85 else r.randbytes(32)
        out.append((k, t))
    return out


def rconsts(r):
    n = r.choice([0, 0, 0, 1, 2, 4, 32])
    out = []
    for _ in range(n):
        k = r.randint(1, 9) if r.random() < 0.9 else r.choice([0, 10, 255, 65535])
        p = r.choice([b"", b"\x01", b"\x01\x02", r.randbytes(r.randint(0, 128)), b"\x01\x02\x03"])
        out.append((k, p))
    return out


def rtype(r, ids, tids):
    tag = r.choice([1, 2, 3, 3, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 11, 12, 13, 14, 14, 0, 15, 255, 0x100, 0x80000003])
    width = r.choice([0, 1, 2, 7, 8, 16, 31, 32, 33, 64, 255, 256, 257, 65535, r.randrange(65536)])
    length = r.choice([0, 1, 10, 65535, 65536, 0xFFFFFFFF])
    elem = r.choice(tids + ids + [r.randbytes(32)]) if (tids or ids) else r.randbytes(32)
    pl = type_pl(tag, width, length, elem)
    if r.random() < 0.08:
        pl = pl[:r.randint(0, 43)]
    elif r.random() < 0.05:
        pl = pl + r.randbytes(r.randint(1, 50))
    return pl


def rvalue(r, ids, tids, wid):
    tid = r.choice(tids) if tids and r.random() < 0.85 else (r.choice(ids) if ids and r.random() < 0.5 else r.randbytes(32))
    w = wid.get(tid)
    bl = (w + 7) // 8 if w is not None and r.random() < 0.7 else r.choice([0, 1, 2, 4, 8, 9, 64, 65, 1000, 65535])
    pl = value_pl(tid, bl, r.randbytes(64))
    if r.random() < 0.06:
        pl = pl[:r.randint(0, 97)]
    elif r.random() < 0.1:
        pl = pl + r.randbytes(r.randint(1, 300))
    return pl


def rop(r, ids, tids):
    opc = r.randint(1, 16) if r.random() < 0.85 else r.choice([0, 17, 255, 0x10000])
    ov = r.randint(1, 3) if r.random() < 0.85 else r.choice([0, 4, 0xFFFFFFFF])
    t = r.choice(tids) if tids else Z
    pl = op_pl(opc, ov, t, r.randint(0, 4), tuple(r.choice(tids or [Z]) for _ in range(4)), t)
    if r.random() < 0.1:
        pl = pl + r.randbytes(r.randint(1, 820))
    return pl


def rapply(r, ids, ops, vals):
    op = r.choice(ops) if ops and r.random() < 0.85 else (r.choice(ids) if ids and r.random() < 0.5 else r.randbytes(32))
    n = r.choice([0, 1, 2, 2, 2, 3, 4, 5, 255]) if r.random() < 0.9 else r.randrange(256)
    pool = vals + ops
    operands = [r.choice(pool) if pool and r.random() < 0.9 else r.randbytes(32) for _ in range(4)]
    pl = apply_pl(op, n, operands)
    if r.random() < 0.2:
        pl = pl + r.randbytes(r.randint(1, 42))
    return pl


def reffect(r):
    c = r.random()
    if c < 0.75:
        pl = r.randint(0, 128)
        return effect_pl(r.randint(1, 65535), r.randrange(65536), r.randrange(1 << 32), r.randrange(1 << 64), r.randbytes(32), r.randbytes(pl))
    if c < 0.80:
        return effect_pl(0, 1, 1, 1)
    if c < 0.85:
        return effect_pl(1, 1, 1, 1, plen=r.randint(129, 65535))
    if c < 0.90:
        e = bytearray(effect_pl(1, 1, 1, 1, params=b"ab"))
        e[50 + r.randint(2, 127)] = r.randint(1, 255)
        return bytes(e)
    if c < 0.95:
        return r.randbytes(176)
    return r.randbytes(r.choice([0, 1, 177, 179, 1024]))


def structured(r, nobj):
    objs, ids, tids, ops, vals, wid = [], [], [], [], [], {}
    for _ in range(nobj):
        c = r.random()
        if c < 0.25:
            o = Obj(KIND_TYPE, payload=rtype(r, ids, tids))
        elif c < 0.45:
            o = Obj(KIND_VALUE, payload=rvalue(r, ids, tids, wid))
        elif c < 0.58:
            o = Obj(KIND_OPERATION, payload=rop(r, ids, tids))
        elif c < 0.70:
            o = Obj(KIND_OPERATION, payload=rapply(r, ids, ops, vals))
        elif c < 0.76:
            o = Obj(KIND_EFFECT, payload=reffect(r))
        elif c < 0.95:
            o = Obj(r.choice([4, 5, 6, 7, 9, 10, 11]), payload=r.randbytes(r.choice([0, 0, 3, 44, 98, 161, 204, 1024])))
        else:
            o = Obj(r.choice([0, 12, 13, 200, 255]), payload=r.randbytes(r.randint(0, 50)))
        if r.random() < 0.6:
            o.attrs = rattrs(r)
        if r.random() < 0.5:
            o.rels = rrels(r, ids)
        if r.random() < 0.3:
            o.consts = rconsts(r)
        if r.random() < 0.03:
            o.attrs = rattrs(r, big=True)
        if r.random() < 0.05:
            o.extra = r.randbytes(r.randint(1, 20))
        i = o.id()
        objs.append(o)
        if i != Z:
            ids.append(i)
            if o.kind == KIND_TYPE:
                tids.append(i)
                tag, w = struct.unpack("<IH", (o.payload + bytes(6))[:6])
                if tag in (3, 4, 5):
                    wid[i] = w
            elif o.kind == KIND_OPERATION:
                ops.append(i)
            elif o.kind == KIND_VALUE:
                vals.append(i)
    if r.random() < 0.1 and objs:
        objs.append(objs[r.randrange(len(objs))])
    return objs


def clean(r, nobj):
    """A graph that validates: types, values, operations, applies, relations, effects."""
    objs = []
    t32 = b_type(3, 32)
    objs.append(t32)
    tids = [t32.id()]
    vals, ops = [], []
    for _ in range(nobj):
        c = r.random()
        if c < 0.2:
            w = r.randint(1, 256)
            o = b_type(r.choice([3, 5]), w)
            tids.append(o.id())
        elif c < 0.45:
            o = b_val_uint(t32.id(), 32, r.randrange(1 << 32))
            vals.append(o.id())
        elif c < 0.6:
            o = b_op(r.randint(1, 16), r.randint(1, 3), r.choice(tids))
            ops.append(o.id())
        elif c < 0.75 and ops and vals:
            o = b_apply(r.choice(ops), r.choice(vals), r.choice(vals + ops))
        elif c < 0.85:
            o = Obj(KIND_EFFECT, payload=effect_pl(r.randint(1, 9), r.randint(0, 9), r.randrange(99), r.randrange(1 << 64), params=r.randbytes(r.randint(0, 128))))
        else:
            o = Obj(KIND_RELATION, rels=[(r.randint(1, 9), r.choice(tids + vals + ops)) for _ in range(r.randint(1, 6))])
        if r.random() < 0.3:
            o.attrs = [(bytes(r.choice(b"abcxyz") for _ in range(r.randint(1, 6))), r.randbytes(r.randint(0, 9))) for _ in range(r.randint(1, 4))]
        if r.random() < 0.2:
            o.consts = [(r.randint(1, 9), r.randbytes(r.randint(0, 6))) for _ in range(r.randint(1, 4))]
        objs.append(o)
    return objs


def cycle_graphs(r):
    """DFS cycles reachable only through the apply-operand read past the 1024-byte payload into the next
    object's attribute value (struct offset 106 + 23 = operand 35's window)."""
    out = []
    for variant in range(12):
        a_pl = bytearray(r.randbytes(1024))
        a_pl[0:8] = struct.pack("<II", r.randint(1, 16), r.randint(1, 3))
        a_pl[32] = 255
        a = Obj(KIND_OPERATION, payload=bytes(a_pl))
        if variant % 3 == 0:      # operand window hits A itself
            tgt = a.id()
            rest = []
        elif variant % 3 == 1:    # hits B, B's relation targets A
            b = Obj(KIND_RELATION, rels=[(r.randint(1, 9), a.id())])
            tgt = b.id()
            rest = [b]
        else:                     # hits a type: no cycle, but the walk follows the window
            t = b_type(3, 8)
            tgt = t.id()
            rest = [t]
        val = r.randbytes(23) + tgt + r.randbytes(r.randint(0, 100))
        nxt = Obj(KIND_MEMORY, attrs=[(b"k", val)])
        out.append(graph([a, nxt] + rest))
    return out


def overread_graphs(r):
    """format_text reads a VALUE's bytes[byte_len] past the payload into the following objects/past the graph."""
    out = []
    t = b_type(3, 32)
    for bl in [990, 991, 1000, 1500, 25000, 25990, 26000, 40000, 51944, 65535]:
        v = Obj(KIND_VALUE, payload=value_pl(t.id(), bl, r.randbytes(64)) + r.randbytes(r.randint(0, 900)))
        nxt = Obj(KIND_MEMORY, attrs=[(rkey(r) or b"k", r.randbytes(512)) for _ in range(3)], rels=[(1, t.id())],
                  consts=[(2, r.randbytes(128))], payload=r.randbytes(1024))
        out.append(graph([t, v, nxt, nxt]))
        out.append(graph([t, v]))
    many = [b_type(3, 32)] + [Obj(KIND_MEMORY, payload=r.randbytes(10)) for _ in range(254)]
    many.append(Obj(KIND_VALUE, payload=value_pl(many[0].id(), 65535, r.randbytes(64))))
    out.append(graph(many))
    return out


def edge_graphs(r):
    out = [b"", b"OMG", b"OMGG", b"OMGG\x00", b"OMGG\x00\x00", b"OMGG\x01\x00", b"OMGG\x01\x01",
           b"OMGH\x00\x00", b"OMGG\x00\x01", b"OMGG\x00\x01\x00\x00\x00\x05OMG0\x01",
           graph([], count=256), graph([], count=257), graph([Obj(1)] * 3, count=2),
           graph([Obj(1)], tail=r.randbytes(100))]
    o = Obj(KIND_VALUE).wire()
    out.append(b"OMGG\x00\x01" + u32be(len(o) + 1000) + o)
    out.append(b"OMGG\x00\x01" + u32be(len(o)) + o[:-1])
    out.append(graph([Obj(KIND_VALUE, magic=b"OMG1")]))
    for ver in range(4):
        for kind in [0, 1, 8, 11, 12, 255]:
            out.append(graph([Obj(kind, ver=ver, payload=effect_pl(1, 1, 1, 1) if kind == 8 else b"")]))
    out.append(graph([Obj(KIND_MEMORY, attrs=[(r.randbytes(64), b"")])]))
    out.append(graph([Obj(KIND_MEMORY, attrs=[(b"k", bytes(513))])]))
    out.append(graph([Obj(KIND_MEMORY, attrs=[(b"k", b"")] * 33)]))
    out.append(graph([Obj(KIND_MEMORY, rels=[(1, Z)] * 65)]))
    out.append(graph([Obj(KIND_MEMORY, consts=[(1, b"")] * 33)]))
    out.append(graph([Obj(KIND_MEMORY, consts=[(1, bytes(129))])]))
    out.append(graph([Obj(KIND_MEMORY, payload=bytes(1025))]))
    out.append(graph([Obj(KIND_MEMORY, payload=bytes(1024))]))
    big = Obj(KIND_MEMORY, attrs=[(bytes([97 + i % 26]) * 20, r.randbytes(512)) for i in range(32)],
              rels=[(r.randint(1, 9), r.randbytes(32)) for _ in range(64)],
              consts=[(r.randint(1, 9), r.randbytes(128)) for _ in range(32)], payload=r.randbytes(1024))
    out.append(graph([big]))
    out.append(graph([big, b_type(3, 8)]))
    t = b_type(3, 32)
    full = [t] + [b_val_uint(t.id(), 32, i) for i in range(255)]
    out.append(graph(full))
    fat = [Obj(KIND_MEMORY, attrs=[(b"k", r.randbytes(400))] * 9) for _ in range(256)]
    out.append(graph(fat))
    for n in [1, 2, 3, 30]:
        out.append(graph([Obj(KIND_MEMORY, attrs=[(bytes([97 + (i * 7) % 26]), bytes([i])) for i in range(n)])]))
    return out


def text_corpus(r):
    base = ("// Non-canonical human representation\n"
            "type U32 = UNSIGNED_INTEGER(32)\n"
            "val a : U32 = 7\n"
            "val b : U32 = 11\n"
            "op add = ADD<U32>[WRAP]\n"
            "apply res = add(a, b)\n")
    out = [base.encode(), base.replace("ADD", "SUB").encode(), b"", b"\n", b"// only\n"]
    nums = ["7", "0", "11", "0x1f", "0X1F", "0x", "0xg", "017", "08", "09", "-1", "-0x10", "+5", " \t\v\f\r\n 42",
            "18446744073709551615", "18446744073709551616", "-18446744073709551616", "99999999999999999999999",
            "4294967296", "4294967295", "0xffffffffffffffff1", "abc", "", "=3", "0777777777777777777777",
            "01777777777777777777777", "02000000000000000000000", "- 5", "--5", "+-5", "0x7fffffff", "\x80",
            "0b101", "0B11", "0b", "0b2", "-0b1", "0b" + "1" * 64, "0b1" + "0" * 64, "0b" + "1" * 33]
    words = ["type", "val", "op", "apply", "types", "value", "opaque", "applyx", "//", "/", "x", "  ", "\t"]
    for _ in range(1200):
        lines = []
        for _ in range(r.randint(0, 14)):
            w = r.choice(words)
            c = r.random()
            if w.startswith("type"):
                lines.append(w + r.choice([" U32 = UNSIGNED_INTEGER(32)", " X = UNSIGNED_INTEGER(16)", "", " UNSIGNED_INTEGER(32"]))
            elif w.startswith("val"):
                lines.append(w + r.choice([" a : U32 = ", " = ", " b ", " c := "]) + r.choice(nums))
            elif w.startswith("op"):
                lines.append(w + r.choice([" add = ADD<U32>[WRAP]", " sub = SUB<U32>[WRAP]", " mul = MUL", " ADDSUB", ""]))
            elif w.startswith("apply"):
                lines.append(w + r.choice([" res = add(a, b)", "", " r"]))
            else:
                lines.append(w + r.choice(["comment", " UNSIGNED_INTEGER(32)", " ADD", " SUB", " = 9", ""]))
            if c < 0.1:
                lines[-1] = r.choice([" ", "\t", "\r", "\v", "\f", "  \n"]) + lines[-1]
        sep = r.choice(["\n", "\n", "\r\n", "\n\n"])
        s = sep.join(lines)
        if r.random() < 0.5:
            s += "\n"
        b = s.encode("latin-1")
        if r.random() < 0.05 and b:
            i = r.randrange(len(b))
            b = b[:i] + b"\0" + b[i:]
        out.append(b)
    for n in [255, 256, 257, 300]:
        out.append((base + "val c : U32 = 5\n" * n + "op ADD\n" * 3 + "apply x\n" * n).encode())
    out.append(("type U32 = UNSIGNED_INTEGER(32)\n" * 300 + "val a = 1\nval b = 2\nop ADD\napply\n").encode())
    out.append(("type U32 = UNSIGNED_INTEGER(32)\n" * 254 + "val a = 1\nval b = 2\nop ADD\nop SUB\napply\n").encode())
    out.append(("type U32 = UNSIGNED_INTEGER(32)\n" * 253 + "val a = 1\nval b = 2\nop ADD\nop ADD\napply\napply\n").encode())
    for _ in range(300):
        out.append(r.randbytes(r.randint(0, 200)))
    for _ in range(100):
        out.append(bytes(r.choice(b"tyvalopplyADSUBN(32)=x0123456789 \n/") for _ in range(r.randint(0, 300))))
    return out


def hexid_corpus(r):
    out = []
    hx = "0123456789abcdefABCDEF"
    pairs = [" 0", "+f", "-f", "-0", "+0", "0x", "0X", " -", " +", "  ", "x1", "g0", "0g", "\t1", "\n2", "\v3", "\f4",
             "\r5", "1 ", "+-", "-+", "- ", "\x801", "1\x80"]
    for _ in range(6000):
        c = r.random()
        if c < 0.35:
            s = "".join(r.choice(hx) for _ in range(64)).encode()
        elif c < 0.75:
            s = bytearray("".join(r.choice(hx) for _ in range(64)).encode("latin-1"))
            for _ in range(r.randint(1, 3)):
                i = r.randrange(32) * 2
                p = r.choice(pairs).encode("latin-1")
                s[i:i + 2] = p
            s = bytes(s)
        elif c < 0.85:
            s = bytes(r.randint(1, 255) for _ in range(64))
        else:
            s = "".join(r.choice(hx) for _ in range(r.choice([0, 1, 62, 63, 65, 66, 100]))).encode()
            if r.random() < 0.3 and s:
                i = r.randrange(len(s))
                s = s[:i] + b"\0" + s[i + 1:]
        win = (s + b"\0" * 65)[:65]
        a = r.randbytes(32)
        d = r.random()
        if d < 0.3:
            b = a
        elif d < 0.8:
            b = bytearray(a)
            b[r.randrange(32)] = r.randrange(256)
            b = bytes(b)
        else:
            b = r.randbytes(32)
        out.append(win + a + b)
    return out


def targeted(r):
    """Every validate branch at its boundaries, and walks that are deep or revisit many duplicates."""
    out = []
    t32 = b_type(3, 32)
    v32 = b_val_uint(t32.id(), 32, 5)
    for tag in range(16):
        for wd in [0, 1, 2, 31, 32, 33, 255, 256, 257]:
            out.append(graph([b_type(tag, wd, 0, t32.id()), t32]))
    for ln in [0, 65535, 65536]:
        for elem in [t32.id(), v32.id(), r.randbytes(32)]:
            out.append(graph([t32, v32, b_type(7, 0, ln, elem)]))
    for tag in [1, 2, 3, 4, 5, 6, 14]:
        for wd in [1, 7, 8, 9, 64, 256, 65535]:
            t = b_type(tag, wd)
            for bl in sorted({(wd + 7) // 8, (wd + 7) // 8 + 1, 0}):
                out.append(graph([t, Obj(KIND_VALUE, payload=value_pl(t.id(), bl))]))
    for opc in [0, 1, 16, 17]:
        for ov in [0, 1, 3, 4]:
            out.append(graph([t32, b_op(opc, ov, t32.id())]))
    op = b_op(3, 1, t32.id())
    rel = Obj(KIND_RELATION, rels=[(1, t32.id())])
    for n in range(6):
        for other in [v32.id(), op.id(), t32.id(), rel.id(), r.randbytes(32)]:
            ops = [v32.id(), other, op.id(), other][:4]
            out.append(graph([t32, v32, op, rel, Obj(KIND_OPERATION, payload=apply_pl(op.id(), n, ops))]))
    for opid in [t32.id(), v32.id(), r.randbytes(32)]:
        out.append(graph([t32, v32, Obj(KIND_OPERATION, payload=apply_pl(opid, 1, [v32.id()]))]))
    chain = [Obj(KIND_RELATION, attrs=[(b"n", b"0")])]
    for i in range(1, 200):
        chain.append(Obj(KIND_RELATION, rels=[(6, chain[-1].id())], attrs=[(b"n", str(i).encode())]))
    out.append(graph(list(reversed(chain))))
    out.append(graph(chain))
    xd = Obj(KIND_MEMORY, payload=b"dup")
    rels = [Obj(KIND_RELATION, rels=[(r.randint(1, 9), xd.id()) for _ in range(64)], attrs=[(b"i", bytes([i]))]) for i in range(100)]
    root = Obj(KIND_RELATION, rels=[(7, o.id()) for o in rels[:64]])
    out.append(graph([root] + rels + [xd] * 120))
    out.append(graph([xd] * 120 + rels + [root]))
    return out


def filler(n):
    """A KIND_MEMORY object whose canonical encoding is exactly n bytes (16 <= n <= 4096)."""
    attrs, left = [], n - 16
    i = 0
    while left > 518 + 1028:
        attrs.append((b"a%02d" % i, bytes([i]) * 512))
        left -= 518
        i += 1
    if left > 1024:
        attrs.append((b"z", bytes(left - 1028)))
        left = 1024
    return Obj(KIND_MEMORY, attrs=attrs, payload=bytes(left))


def boundary(r):
    """Serialize totals around the 8192-byte product buffer, and format_text where a stale line length
    equals the room left (the C's `w >= max_len - pos` at equality)."""
    out = []
    for total in range(8186, 8200):
        a = 4089
        b = total - 6 - 8 - a
        g = graph([filler(a), filler(b)])
        assert len(g) == total and len(filler(a).canonical()) == a and len(filler(b).canonical()) == b
        out.append(g)
    t = b_type(3, 32)
    for m, val in [(45, 500), (45, 99), (45, 1000), (44, 500)]:
        objs = [Obj(KIND_MEMORY) for _ in range(m)]
        objs.append(Obj(KIND_VALUE, payload=value_pl(t.id(), 2, bytes([val >> 8 & 255, val & 255]))))
        objs.append(Obj(KIND_OPERATION, payload=r.randbytes(r.choice([0, 160]))))
        out.append(graph(objs))
    return out


def tree_files(root):
    """Every .omega/.osc/.bin file under ROOT/tests and ROOT/examples, sorted by path."""
    out = []
    for top in ("tests", "examples"):
        for dp, dn, fn in os.walk(os.path.join(root, top)):
            for f in fn:
                if f.endswith((".omega", ".osc", ".bin")):
                    out.append(os.path.relpath(os.path.join(dp, f), root))
    return sorted(out)


def pack(path, recs):
    with open(path, "wb") as f:
        for b in recs:
            f.write(struct.pack("<I", len(b)) + b)
    return len(recs), sum(len(b) for b in recs)


def main():
    args = sys.argv[1:]
    if len(args) == 3 and args[1] == "--tree":
        recs = tree_files(args[2])
        n = pack(os.path.join(args[0], "tree.pack"), [open(os.path.join(args[2], f), "rb").read() for f in recs])
        with open(os.path.join(args[0], "tree.list"), "w") as f:
            f.write("\n".join(recs) + "\n")
        print("tree.pack: %d records, %d bytes" % n)
        return
    if len(args) not in (1, 3) or (len(args) == 3 and args[1] != "--check-product"):
        sys.exit(__doc__)
    od = args[0]
    os.makedirs(od, exist_ok=True)
    r = random.Random(726_0001)
    pv = product_vectors()
    if len(args) == 3:
        for name, b in pv.items():
            got = open(os.path.join(args[2], name), "rb").read()
            if got != b:
                sys.exit("corpus.py: %s differs from the C product builders" % name)
        print("product vectors: %d equal to the C builders' bytes" % len(pv))
    dec = list(pv.values())
    valid = list(pv.values())
    dec += edge_graphs(r)
    dec += cycle_graphs(r)
    dec += overread_graphs(r)
    dec += targeted(r)
    dec += boundary(r)
    for _ in range(300):
        g = graph(clean(r, r.randint(0, 30)))
        dec.append(g)
        valid.append(g)
    for _ in range(700):
        dec.append(graph(structured(r, r.choice([1, 1, 2, 3, 5, 8, 12, 20, 40]))))
    small = [g for g in valid if len(g) <= 4000]
    for _ in range(2000):
        g = bytearray(r.choice(small))
        i = r.randrange(len(g))
        g[i] = (g[i] + r.randint(1, 255)) & 255
        dec.append(bytes(g))
    for _ in range(300):
        g = r.choice(small)
        dec.append(g[:r.randrange(len(g))])
    for _ in range(500):
        dec.append(r.randbytes(r.randint(0, 300)))
    for _ in range(300):
        n = r.randint(0, 3)
        b = bytearray(b"OMGG" + u16be(n))
        for _ in range(n):
            body = b"OMG0" + bytes([r.choice([1, 1, 2, r.randrange(256)]), r.choice([1, 2, 3, 6, 8, r.randrange(256)])]) + r.randbytes(r.randint(0, 120))
            b += u32be(len(body) if r.random() < 0.9 else r.randint(0, 200)) + body
        dec.append(bytes(b))
    st = {}
    st["decode"] = pack(os.path.join(od, "decode.pack"), dec)
    st["text"] = pack(os.path.join(od, "text.pack"), text_corpus(r))
    st["hexid"] = pack(os.path.join(od, "hexid.pack"), hexid_corpus(r))
    for k, (n, b) in st.items():
        print("%s.pack: %d records, %d bytes" % (k, n, b))


if __name__ == "__main__":
    main()
