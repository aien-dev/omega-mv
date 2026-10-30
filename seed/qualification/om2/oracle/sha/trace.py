#!/usr/bin/python3
# SPDX-License-Identifier: Apache-2.0
# ORACLE (outside the delivered closure), OM-2 qualification L836 (#748), phaseB/sha-f5d14cb2.
# Contract section 8 evidence (b): dynamic comparison of instruction-address and data-address traces
# for seeded message pairs with equal lengths, op codes and call sequence (HOST, single-step ptrace).
# The child (trace_child.c) stops itself with SIGSTOP before and after each traced call sequence; this script
# single-steps it (PTRACE_SINGLESTEP) and records, for every executed instruction inside the embedded
# OM-2 code, its byte offset in the artifact and, for loads and stores, the effective address computed
# from the register file before the instruction and the operand form printed by GNU objdump 2.42 for
# that exact artifact word. It also records the whole-process program-counter sequence (binding,
# libc). Segments come in pairs (message A, message B); each pair must be identical in all three.
# It also reports, per export, the deepest SP observed below the SP at that export's entry word
# (a dynamic cross-check of stack.py's static bound, not a bound itself).
# Usage: trace.py CHILD LIB ARTIFACT        exit 0 iff every pair is identical
import ctypes
import os
import re
import signal
import struct
import subprocess
import sys

CHILD, LIB, ART = sys.argv[1], os.path.realpath(sys.argv[2]), sys.argv[3]
libc = ctypes.CDLL(None, use_errno=True)
libc.ptrace.restype = ctypes.c_long
libc.ptrace.argtypes = [ctypes.c_long, ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p]
TRACEME, PEEK, CONT, STEP, GETREGSET = 0, 2, 7, 9, 0x4204
NT_PRSTATUS = 1


class Iov(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("len", ctypes.c_size_t)]


regs = (ctypes.c_uint64 * 34)()
iov = Iov(ctypes.cast(regs, ctypes.c_void_p), ctypes.sizeof(regs))


def getregs(pid):
    if libc.ptrace(GETREGSET, pid, NT_PRSTATUS, ctypes.byref(iov)) != 0:
        raise OSError(ctypes.get_errno(), "GETREGSET")
    return regs  # x0..x30, sp (31), pc (32), pstate (33)


# objdump operand forms of the artifact's memory instructions
art = open(ART, "rb").read()
dis = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "aarch64", ART], check=True,
                     capture_output=True, text=True).stdout
MEMOP = {}
rx = re.compile(r"^\s*([0-9a-f]+):\t[0-9a-f]{8} \t(\S+)\s*(.*)$")
for line in dis.splitlines():
    m = rx.match(line)
    if not m:
        continue
    off, mn, ops = int(m.group(1), 16), m.group(2), m.group(3)
    if not re.match(r"^(ld|st)", mn) or "[" not in ops:
        if re.match(r"^ldr", mn) and "[" not in ops:
            MEMOP[off] = ("lit", ops)
        continue
    inner = ops[ops.index("[") + 1:ops.index("]")]
    post = ops[ops.index("]") + 1:].strip()
    parts = [p.strip() for p in inner.split(",")]
    MEMOP[off] = ("mem", parts, post)


def reg(r, name):
    name = name.strip()
    if name in ("sp", "wsp"):
        return r[31]
    if name in ("xzr", "wzr"):
        return 0
    v = r[int(name[1:])]
    return v & 0xFFFFFFFF if name[0] == "w" else v


def ea(r, op):
    if op[0] == "lit":
        return int(op[1].split(",")[1].split()[0], 16) if "," in op[1] else -1
    _, parts, post = op
    base = reg(r, parts[0])
    if len(parts) == 1 or post.startswith("#"):
        return base  # [xn] or post-index: the access is at the base
    p1 = parts[1]
    if p1.startswith("#"):
        return (base + int(p1[1:], 0)) & (2**64 - 1)
    idx = reg(r, p1)
    sh = 0
    if len(parts) > 2:
        ext = parts[2].split()
        kind = ext[0]
        if kind == "sxtw":
            idx = idx - (1 << 32) if idx & (1 << 31) else idx
        elif kind == "uxtw":
            idx &= 0xFFFFFFFF
        if len(ext) > 1:
            sh = int(ext[1][1:], 0)
    return (base + (idx << sh)) & (2**64 - 1)


pid = os.fork()
if pid == 0:
    libc.ptrace(TRACEME, 0, None, None)
    os.execv(CHILD, [CHILD])
    os._exit(127)

_, st = os.waitpid(pid, 0)  # exec stop
assert os.WIFSTOPPED(st), st
libc.ptrace(CONT, pid, None, None)
_, st = os.waitpid(pid, 0)  # first SIGSTOP: the library is mapped now
assert os.WIFSTOPPED(st) and os.WSTOPSIG(st) == signal.SIGSTOP, st

bias = None
for line in open(f"/proc/{pid}/maps"):
    f = line.split()
    if len(f) >= 6 and os.path.realpath(f[5]) == LIB and int(f[2], 16) == 0:
        bias = int(f[0].split("-")[0], 16)
        break
sym = subprocess.run(["objdump", "-t", LIB], check=True, capture_output=True, text=True).stdout
code_off = int([l.split()[0] for l in sym.splitlines() if l.split() and l.split()[-1] == "ombr2_code_sha256_om2"][0], 16)
lo = bias + code_off
hi = lo + len(art)
with open(f"/proc/{pid}/mem", "rb") as mem:
    mem.seek(lo)
    live = mem.read(len(art))
print(f"INFO code at {lo:#x}..{hi:#x} ({len(art)} bytes); live bytes == artifact: {live == art}")
if live != art:
    sys.exit(1)

EXPORTS = ["sha256_init", "sha256_update", "sha256_final", "sha256_hash"]  # entry words 0..3, source order
maxdepth = {}
cur_export, entry_sp = None, 0
segs = []
cur = None
while True:
    # at a SIGSTOP stop: start a new segment, suppress the signal
    cur = {"code": [], "pcs": []}
    segs.append(cur)
    while True:
        libc.ptrace(STEP, pid, None, None)
        _, st = os.waitpid(pid, 0)
        if os.WIFEXITED(st) or os.WIFSIGNALED(st):
            break
        sig = os.WSTOPSIG(st)
        if sig == signal.SIGSTOP:
            break
        if sig != signal.SIGTRAP:
            print(f"FAIL unexpected signal {sig}")
            sys.exit(1)
        r = getregs(pid)
        pc = r[32]
        cur["pcs"].append(pc)
        if lo <= pc < hi:
            off = pc - lo
            op = MEMOP.get(off)
            cur["code"].append((off, ea(r, op)) if op else (off,))
            if off < 4 * len(EXPORTS):  # entry word of export off/4: SP at the call's entry
                cur_export, entry_sp = off // 4, r[31]
            depth = entry_sp - r[31]
            if depth > maxdepth.get(cur_export, -1):
                maxdepth[cur_export] = depth
    if os.WIFEXITED(st) or os.WIFSIGNALED(st):
        break

segs = [s for s in segs if s["code"]]  # the final segment (exit path) runs no OM-2 code
print(f"INFO child exit status {os.WEXITSTATUS(st) if os.WIFEXITED(st) else 'signal'}; segments with OM-2 code: {len(segs)}")
if len(segs) % 2 or not segs:
    print("FAIL odd or empty segment count")
    sys.exit(1)
bad = 0
for i in range(0, len(segs), 2):
    a, b = segs[i], segs[i + 1]
    ins_a = [x[0] for x in a["code"]]
    ins_b = [x[0] for x in b["code"]]
    da = [x[1] for x in a["code"] if len(x) > 1]
    db = [x[1] for x in b["code"] if len(x) > 1]
    same = (ins_a == ins_b, da == db, a["pcs"] == b["pcs"])
    tag = "PASS" if all(same) else "FAIL"
    if not all(same):
        bad += 1
        k = next((j for j in range(min(len(ins_a), len(ins_b))) if a["code"][j] != b["code"][j]), min(len(ins_a), len(ins_b)))
        print(f"  first difference at OM-2 step {k}: {a['code'][k:k+1]} vs {b['code'][k:k+1]}")
    print(f"{tag} pair {i // 2}: om2-instructions={len(ins_a)}/{len(ins_b)} data-addresses={len(da)}/{len(db)} "
          f"process-pcs={len(a['pcs'])}/{len(b['pcs'])} equal(insn,data,pcs)={same}")
for k in sorted(maxdepth):
    print(f"INFO observed max stack depth below entry SP, export {EXPORTS[k]}: {maxdepth[k]} bytes")
print(f"{'PASS' if bad == 0 else 'FAIL'} trace pairs={len(segs) // 2} differing={bad}")
sys.exit(1 if bad else 0)
