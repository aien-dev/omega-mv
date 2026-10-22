#!/usr/bin/env python3
"""Synthetic cubin images whose .nv.info records carry kernel parameters.
Writes the bytes only. Expected values come from the Wolfram decode, not from here.
"""
import importlib.util, json, pathlib, sys

spec = importlib.util.spec_from_file_location(
    "gf", str(pathlib.Path(__file__).with_name("gen_fixtures.py")))
gf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gf)


def u(n, w):
    return (n & ((1 << (8 * w)) - 1)).to_bytes(w, "little")


def sval(attr, payload):
    return bytes([4, attr, len(payload) & 255, len(payload) >> 8]) + payload + bytes((-len(payload)) % 4)


def hval(attr, v):
    return bytes([3, attr]) + u(v, 2)


def nval(attr):
    return bytes([1, attr])


def kparam(ordinal, offset, size, smem=0, cbank=31):
    packed = ((size & 0x3FFF) << 18) | ((smem & 1) << 17) | ((cbank & 0x1F) << 12)
    return sval(0x17, u(0, 4) + u(ordinal, 2) + u(offset, 2) + u(packed, 4))


def regcount(sym, n):
    return sval(0x2F, u(sym, 4) + u(n, 4))


def frame(sym):
    return sval(0x11, u(sym, 4) + u(0, 4))


def param_cbank(sym, base, size):
    return sval(0x0A, u(sym, 4) + u(base, 2) + u(size, 2))


def cbank_size(size):
    return hval(0x19, size)


def image(parts):
    """parts: ('text', name, sh_info) or ('info', name, bytes)."""
    e = gf.Elf()
    for kind, name, extra in parts:
        if kind == "text":
            e.add(name, 1, bytes(16), info=extra)
        else:
            e.add(name, 0x70000000, extra)
    return e.bytes()[0]


def good_info(nparams=2, size=None, base=0x380, block=None, smem=0, cbank=31, ordinals=None, dup=False):
    if ordinals is None:
        ordinals = list(range(nparams - 1, -1, -1))
    if size is None:
        size = 8 * nparams
    if block is None:
        block = size
    body = b""
    for i, ordinal in enumerate(ordinals):
        body += kparam(ordinal, 8 * ordinal, 8, smem=smem if i == 0 else 0, cbank=cbank)
    if dup:
        body += kparam(0, 0, 8)
    body += cbank_size(size) + param_cbank(9, base, block)
    return body


def main():
    fx = {}
    ginfo = regcount(1, 14) + frame(1)
    fx["PGOOD"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", good_info()),
    ]).hex()
    fx["PZERO"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", regcount(1, 8)),
        ("info", ".nv.info.k", cbank_size(0) + param_cbank(9, 0x380, 0)),
    ]).hex()
    fx["PNOINFO"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", regcount(1, 14)),
    ]).hex()
    fx["PNOREGS"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", frame(1)),
        ("info", ".nv.info.k", good_info()),
    ]).hex()
    fx["PBADFMT"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", bytes([5, 0, 0, 0]) + good_info()),
    ]).hex()
    fx["PDISAGREE"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", good_info(block=8)),
    ]).hex()
    fx["PSMEM"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", good_info(nparams=1, smem=1)),
    ]).hex()
    fx["PGAP"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", good_info(ordinals=[2, 0], size=24)),
    ]).hex()
    fx["PDUP"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", good_info(nparams=1, dup=True)),
    ]).hex()
    fx["PFIT"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", kparam(0, 8, 16) + cbank_size(16) + param_cbank(9, 0x380, 16)),
    ]).hex()
    fx["PNVAL"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", nval(0) + good_info(nparams=1, size=8)),
    ]).hex()
    fx["PPAD"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", sval(0x36, b"\x01") + good_info(nparams=1, size=8)),
    ]).hex()
    fx["PPREFIX"] = image([
        ("text", ".text.k", 1),
        ("text", ".text.kX", 2),
        ("info", ".nv.info", regcount(1, 10) + regcount(2, 20)),
        ("info", ".nv.info.kX", good_info(nparams=1, size=8)),
        ("info", ".nv.info.k", kparam(0, 0, 4) + cbank_size(4) + param_cbank(9, 0x380, 4)),
    ]).hex()
    raw = bytearray(bytes.fromhex(fx["PGOOD"]))
    # truncate the file so the last info record is cut: drop the last 6 bytes
    fx["PTRUNC"] = bytes(raw[:-6]).hex()
    fx["PTWOREGS"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", regcount(1, 14) + regcount(1, 15)),
        ("info", ".nv.info.k", good_info()),
    ]).hex()
    fx["PBCANK"] = image([
        ("text", ".text.k", 1),
        ("info", ".nv.info", ginfo),
        ("info", ".nv.info.k", good_info(nparams=1, cbank=0)),
    ]).hex()
    out = pathlib.Path(__file__).with_name("param_fixtures.json")
    out.write_text(json.dumps(fx))
    print("images", len(fx), "longest", max(len(bytes.fromhex(v)) for v in fx.values()))


if __name__ == "__main__":
    main()
