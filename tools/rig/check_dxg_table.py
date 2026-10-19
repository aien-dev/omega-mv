#!/usr/bin/env python3
"""Cross-check the ioctl rows the dxg backend uses against the WSL2 kernel's dispatch table.

usage: check_dxg_table.py <d3dkmthk.h> <drivers/hv/dxgkrnl/ioctl.c>
Reads the LX_* macro numbers from the header, and the `/* 0xNN */ {handler, LX_NAME}` table from
ioctl.c, and checks that each ioctl the backend uses has the same number in both and a handler.
Prints one line per ioctl and exits 1 on any mismatch."""
import re, sys
hdr = open(sys.argv[1]).read()
src = open(sys.argv[2]).read()
nums = {}
for m in re.finditer(r'#define\s+(LX_\w+)\s*\\?\s*_IOWR\(0x47,\s*(0x[0-9a-fA-F]+),\s*struct\s+(\w+)\)', hdr):
    nums[m.group(1)] = (int(m.group(2), 16), m.group(3))
table = {}
for m in re.finditer(r'/\*\s*(0x[0-9a-fA-F]+)\s*\*/\s*\{\s*(\w+),\s*(LX_\w+)', src):
    table[m.group(3)] = (int(m.group(1), 16), m.group(2))
used = """LX_DXENUMADAPTERS2 LX_DXOPENADAPTERFROMLUID LX_DXQUERYADAPTERINFO LX_DXCREATEDEVICE LX_DXCREATEPAGINGQUEUE
LX_DXCREATECONTEXTVIRTUAL LX_DXCREATESYNCHRONIZATIONOBJECT LX_DXCREATEALLOCATION LX_DXMAPGPUVIRTUALADDRESS
LX_DXMAKERESIDENT LX_DXLOCK2 LX_DXUNLOCK2 LX_DXSUBMITCOMMAND LX_DXSIGNALSYNCHRONIZATIONOBJECTFROMGPU
LX_DXWAITFORSYNCHRONIZATIONOBJECTFROMCPU LX_DXDESTROYALLOCATION2 LX_DXDESTROYSYNCHRONIZATIONOBJECT LX_DXDESTROYCONTEXT
LX_DXDESTROYPAGINGQUEUE LX_DXDESTROYDEVICE LX_DXCLOSEADAPTER LX_ISFEATUREENABLED LX_DXCREATEHWQUEUE
LX_DXSUBMITCOMMANDTOHWQUEUE LX_DXDESTROYHWQUEUE""".split()
bad = 0
for n in used:
    h = nums.get(n); t = table.get(n)
    ok = h and t and h[0] == t[0]
    print(("ok   " if ok else "BAD  ") + f"{n:44s} header nr={h[0]:#04x} struct={h[1]:42s} kernel handler={t[1] if t else None} nr={t[0]:#04x}" if h and t else f"BAD  {n} header={h} table={t}")
    bad += 0 if ok else 1
sys.exit(1 if bad else 0)
