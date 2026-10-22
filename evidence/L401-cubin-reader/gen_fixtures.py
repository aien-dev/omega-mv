#!/usr/bin/env python3
"""Synthetic ELF64 cubin fixtures for the Omega reader tests. The bytes are
generated here; the expected answers are derived by Wolfram, not by this script.
ELF layout (little endian, 64-bit):
  header 64 B; section header table at `shoff`, each entry 64 B;
  section name string table (type 3) at the index named by e_shstrndx.
  A section's name is the NUL-terminated string at shstrtab[sh_name].
  A text section has a name starting ".text."; type 8 (NOBITS) has no file bytes.
"""
import json, struct, pathlib

def u(n, w):
    return (n & ((1 << (8 * w)) - 1)).to_bytes(w, "little")

class Elf:
    def __init__(self, arch=121, abi=8, machine=190, cls=2, data=1, version=1,
                 ehsize=64, shentsize=64, osabi=0x41):
        # e_ident[7] = EI_OSABI (0x41 from CUDA 13 nvcc, 0x33 before), e_ident[8] = EI_ABIVERSION
        # (7: sm in e_flags bits 0-7; 8: sm in bits 8-15). Checked against a real sm_121 cubin.
        self.osabi = osabi
        self.arch, self.abi, self.machine = arch, abi, machine
        self.cls, self.data, self.version = cls, data, version
        self.ehsize, self.shentsize = ehsize, shentsize
        self.sections = [("NULL", 0, 0, 0, 0)]  # name, type, offset, size, info
        self.blobs = {}
    def add(self, name, typ, data=b"", info=0, size=None, offset=None):
        self.sections.append((name, typ, offset, len(data) if size is None else size, info))
        self.blobs[len(self.sections) - 1] = data
    def bytes(self):
        shoff = 64
        nsec = len(self.sections) + 1  # plus the string table appended below
        body = bytearray()
        start = shoff + 64 * nsec
        out_secs = []
        for i, (name, typ, off, size, info) in enumerate(self.sections):
            data = self.blobs.get(i, b"")
            if off is None and typ != 8 and data:
                off = start + len(body)
                body += data
            out_secs.append((name, typ, 0 if off is None else off, size, info))
        names = b"\x00"
        idx = {}
        for name, *_ in out_secs:
            if name not in idx and name != "NULL":
                idx[name] = len(names)
                names += name.encode() + b"\x00"
        idx["NULL"] = 0
        str_off = start + len(body)
        body += names
        out_secs[0] = (".shstrtab", 3, str_off, len(names), 0)
        # rebuild with the string table as section 0's slot? No: keep NULL at 0 and
        # append the string table as the last section.
        out_secs[0] = ("NULL", 0, 0, 0, 0)
        out_secs.append((".shstrtab", 3, str_off, len(names), 0))
        shstrndx = len(out_secs) - 1
        sh = b""
        for name, typ, off, size, info in out_secs:
            sh += u(idx.get(name, 0), 4) + u(typ, 4) + u(0, 8) + u(0, 8)
            sh += u(off, 8) + u(size, 8) + u(0, 4) + u(info, 4) + u(1, 8) + u(0, 8)
        flags = self.arch << 8 if self.abi >= 8 else self.arch
        hdr = (b"\x7fELF" + bytes([self.cls, self.data, self.version, self.osabi, self.abi]) + bytes(7)
               + u(2, 2) + u(self.machine, 2) + u(1, 4) + bytes(16)
               + u(shoff, 8) + u(flags, 4) + u(self.ehsize, 2) + u(0, 2) + u(0, 2)
               + u(self.shentsize, 2) + u(len(out_secs), 2) + u(shstrndx, 2))
        assert len(hdr) == 64 and len(sh) == 64 * len(out_secs)
        return bytes(hdr + sh + body), len(out_secs)

def main():
    fx = {}
    def put(name, e, **k):
        b, n = e.bytes()
        fx[name] = b.hex()
        return b
    g = Elf()
    g.add(".text.k", 1, bytes(32), info=16 << 24)
    good, nsec = g.bytes()
    fx["GOOD"] = good.hex()
    b = Elf(abi=7, arch=120, osabi=0x33)
    b.add(".text.k", 1, bytes(32), info=16 << 24)
    fx["ABI7"] = b.bytes()[0].hex()
    b = Elf(arch=90)
    b.add(".text.k", 1, bytes(32), info=16 << 24)
    fx["SM90"] = b.bytes()[0].hex()
    b = Elf()
    b.add(".nv.shared.k", 8, size=2**40)
    fx["NOBITS"] = b.bytes()[0].hex()
    b = Elf()
    b.add(".text.a", 1, bytes(16), info=8 << 24)
    b.add(".text.b", 1, bytes(16), info=8 << 24)
    fx["TWO"] = b.bytes()[0].hex()
    b = Elf()
    b.add(".text.k", 1, bytes(32), info=16 << 24, offset=10**15)
    fx["PASTEND"] = b.bytes()[0].hex()
    b = Elf()
    b.add(".text.k", 1, bytes(32), info=16 << 24, offset=40)
    fx["OVERLAP"] = b.bytes()[0].hex()
    raw = bytearray(good)
    raw[0] = 0
    fx["BADMAGIC"] = bytes(raw).hex()
    raw = bytearray(good)
    raw[5] = 2
    fx["BIGENDIAN"] = bytes(raw).hex()
    raw = bytearray(good)
    raw[4] = 1
    fx["ELF32"] = bytes(raw).hex()
    raw = bytearray(good)
    struct.pack_into("<H", raw, 18, 183)
    fx["BADMACHINE"] = bytes(raw).hex()
    raw = bytearray(good)
    struct.pack_into("<H", raw, 58, 32)
    fx["BADSHENTSIZE"] = bytes(raw).hex()
    raw = bytearray(good)
    struct.pack_into("<H", raw, 60, 0)
    fx["ZEROSHN"] = bytes(raw).hex()
    raw = bytearray(good)
    struct.pack_into("<Q", raw, 40, 10**15)
    fx["SHOFFHUGE"] = bytes(raw).hex()
    raw = bytearray(good)
    struct.pack_into("<H", raw, 60, 65535)
    fx["SHNWRAP"] = bytes(raw).hex()
    raw = bytearray(good)
    struct.pack_into("<H", raw, 62, 9)
    fx["BADSTRNDX"] = bytes(raw).hex()
    raw = bytearray(good)
    struct.pack_into("<I", raw, 64 + 64 + 0, 10**6)
    fx["BADNAME"] = bytes(raw).hex()
    fx["TRUNC"] = good[:40].hex()
    fx["EMPTY"] = ""
    # negative-control fixtures: each one separates a correct bound from a plausible wrong one
    b = Elf(abi=9)
    b.add(".text.k", 1, bytes(32), info=16 << 24)
    fx["BADABI"] = b.bytes()[0].hex()            # unknown ABI: arch position unknown, refuse
    b = Elf()
    b.add(".text.k", 1, bytes(32), info=16 << 24, offset=32)
    fx["HDROVER"] = b.bytes()[0].hex()           # section bytes inside the ELF header
    b = Elf()
    b.add(".text.k", 1, bytes(32), info=16 << 24, offset=2**64 - 16)
    fx["SECWRAP"] = b.bytes()[0].hex()           # off + size wraps to 16: a naive off+size<=n accepts
    raw = bytearray(good)
    struct.pack_into("<Q", raw, 40, 2**64 - 64)
    fx["SHOFFWRAP"] = bytes(raw).hex()           # shoff + 64*shnum wraps to 128: naive check accepts
    raw = bytearray(good)
    struct.pack_into("<H", raw, 62, 1)
    fx["STRTYPE"] = bytes(raw).hex()             # e_shstrndx names .text.k (type 1, not 3)
    b = Elf()
    b.add(".text", 1, bytes(16), info=8 << 24)
    b.add(".nv.text.k", 1, bytes(16), info=8 << 24)
    b.add(".text.n", 8, size=64)
    fx["NOTTEXT"] = b.bytes()[0].hex()           # no ".text." prefix, or NOBITS: text_count 0
    out = pathlib.Path(__file__).with_name("fixtures.json")
    out.write_text(json.dumps({"fixtures": fx, "notes": {
        "GOOD": "sm_121 ABI8, one .text.k of 32 bytes, expect arch 121 text_count 1",
        "ABI7": "sm_120 ABI7 (arch in low byte), expect arch 120 text_count 1",
        "SM90": "arch 90, header valid, expect arch 90 text_count 1",
        "NOBITS": "one type-8 section of declared size 2^40, expect text_count 0 layout_ok 1",
        "TWO": "two .text sections, expect text_count 2",
        "PASTEND": ".text offset 10^15, expect layout_ok 0",
        "OVERLAP": ".text offset 40 inside the section table, expect layout_ok 0",
        "BADMAGIC": "first byte 0, expect hdr_ok 0",
        "BIGENDIAN": "EI_DATA 2, expect hdr_ok 0",
        "ELF32": "EI_CLASS 1, expect hdr_ok 0",
        "BADMACHINE": "e_machine 183, expect hdr_ok 0",
        "BADSHENTSIZE": "e_shentsize 32, expect hdr_ok 0",
        "ZEROSHN": "e_shnum 0, expect hdr_ok 0",
        "SHOFFHUGE": "e_shoff 10^15, expect hdr_ok 0",
        "SHNWRAP": "e_shnum 65535, expect hdr_ok 0 (table would wrap past file)",
        "BADSTRNDX": "e_shstrndx 9 past e_shnum, expect hdr_ok 0",
        "BADNAME": "sh_name 10^6 past string table, expect layout_ok 0",
        "TRUNC": "first 40 bytes only, expect hdr_ok 0",
        "EMPTY": "length 0, expect hdr_ok 0"}}, indent=1))
    print("fixtures", len(fx), "GOOD bytes", len(good), "nsec", nsec)

if __name__ == "__main__":
    main()
