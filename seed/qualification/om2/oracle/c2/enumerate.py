#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Enumeration tool for the OM-2 C2 manifest (#748, ledger L836). NOT part of the delivered closure:
# it ran once, outside it, to freeze seed/qualification/om2/manifest-c2.txt. The delivered checker is
# seed/qualification/om2/gate.om (emit cases). See CAPTURE.txt for how the inputs were collected.
#
# Inputs (all read-only):
#   CAP     capture worktree at acb3f15 (git worktree --detach), whose seed/out/omc was a recording
#           shim in front of the pinned omc; the shim wrote every stdin it got to CORPUS/in.* and the
#           gate name to in.*.who
#   CORPUS  the recorded inputs
#   G5      the gate5 inline programs (gate5.sh section 3b, 5d, 7), written by the same commands
#   BRIDGE  an omega-bridge clone; every .om at the om/omega-bridge.lock commit 012860db is added
# Every distinct input (by sha256 of its bytes) except seed/omc/omc.om is compiled once by the pinned
# compiler OMC (sha256 checked) and recorded: exit status, sha256 of stdout, exact stderr.
import hashlib, os, re, subprocess, sys

OMC_PIN = "734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61"
CAP, CORPUS, G5, BRIDGE, OMC, OUT = sys.argv[1:7]
BRIDGE_REV = "012860dbf26ee62a9838594b9dfd1e3e94a540a9"
PACKREL = "seed/qualification/om2/c2/corpus.pack"
CYC = {  # inputs above 1 MiB: their longest run of one of these units becomes a cyc: segment
    "nl": ("seed/qualification/om2/c2/cyc-nl.txt", b"\n"),
    "filler": ("seed/qualification/om2/c2/cyc-filler.txt", b"note filler\n"),
    "stmt": ("seed/qualification/om2/c2/cyc-stmt.txt", b"x = add x 1\n"),
}

def sha(b):
    return hashlib.sha256(b).hexdigest()

assert sha(open(OMC, "rb").read()) == OMC_PIN, "OMC is not the pinned compiler"
omc_om = open(os.path.join(CAP, "seed/omc/omc.om"), "rb").read()

units = {}   # sha -> bytes
origins = {} # sha -> [origin]
order = []

def add(data, origin):
    h = sha(data)
    if data == omc_om:
        return
    if h not in units:
        units[h] = data
        origins[h] = []
        order.append(h)
    if origin not in origins[h]:
        origins[h].append(origin)

# 1. static: every tracked .om / .omegese of the baseline tree (seed/rung4/tests, seed/omc/tests,
#    om/**, security673, tests/one_layer, tools/exp445, seed/host/tests, ...)
files = subprocess.run(["git", "-C", CAP, "ls-files", "*.om", "*.omegese"], capture_output=True,
                       text=True, check=True).stdout.split()
for f in sorted(files):
    data = subprocess.run(["git", "-C", CAP, "show", "HEAD:" + f], capture_output=True, check=True).stdout
    add(data, "tree:" + f)
# 2. omega-bridge at the lock commit (om-units generator src/native.om and the bridge's own units)
bfiles = subprocess.run(["git", "-C", BRIDGE, "ls-tree", "-r", "--name-only", BRIDGE_REV],
                        capture_output=True, text=True, check=True).stdout.split()
for f in sorted(x for x in bfiles if x.endswith(".om")):
    data = subprocess.run(["git", "-C", BRIDGE, "show", BRIDGE_REV + ":" + f], capture_output=True,
                          check=True).stdout
    add(data, "bridge@012860db:" + f)
# 3. recorded inputs of the seed gates (gate4 capture, gate6-17, gate-ir, host gate, nextfuzz)
rec = sorted(n for n in os.listdir(CORPUS) if n.startswith("in.") and not n.endswith(".who"))
rec.sort(key=lambda n: os.stat(os.path.join(CORPUS, n)).st_mtime_ns)
for n in rec:
    who = open(os.path.join(CORPUS, n + ".who")).read().split()[-1]
    add(open(os.path.join(CORPUS, n), "rb").read(), "gate:" + who)
# 4. gate669 (security673): every stdin of a compiler child (1001 stage1, 1002 stage2, 1007 and 1076
#    branch-limit mutant compilers built from branch-mutate.om output)
for line in open(os.path.join(CAP, "build/gate669.log")):
    m = re.match(r"^build/security673-native/(1001|1002|1007|1076) < (\S+) > ", line)
    if m:
        add(open(os.path.join(CAP, m.group(2)), "rb").read(), "gate669:" + m.group(2))
# 5. gate5 inline programs (gate5.sh 3b, 5d, 7) and the two capacity inputs over 8 MiB
for n in ["ok", "e4", "e11", "e12", "e13", "bad", "m", "zm", "e14"]:
    add(open(os.path.join(G5, n + ".om"), "rb").read(), "gate5:" + n + ".om")
# 6. the #748 lane's own OM-1 units (appended last, so entries of 1-5 keep their ids): every .om under
#    OUT (seed/qualification/om2) and every SRC (a+b concatenation) of its case lists whose first line
#    that is not a note or blank (E10) does not start with the word om2
def is_om2(data):
    for ln in data.split(b"\n"):
        w = ln.split()
        if not w or w[0] == b"note":
            continue
        return w[0] == b"om2"
    return False

def qual_src(spec):
    if spec.startswith(("pack:", "cyc:")):
        return None
    data = b""
    for p in spec.split("+"):
        f = os.path.join(OUT, "../../..", p)
        if not p.startswith("seed/qualification/om2/") and not p.startswith("om/"):
            return None
        if not os.path.isfile(f):
            return None
        data += open(f, "rb").read()
    return data

qroot = os.path.abspath(OUT)
for dp, dn, fn in sorted(os.walk(qroot)):
    dn.sort()
    if os.path.relpath(dp, qroot).split(os.sep)[0] in ("c2", "evidence"):
        continue
    for f in sorted(fn):
        p = os.path.join(dp, f)
        rel = "seed/qualification/om2/" + os.path.relpath(p, qroot)
        if f.endswith(".om"):
            data = open(p, "rb").read()
            if not is_om2(data):
                add(data, "qual:" + rel)
        elif f.endswith(".cases") and f != "manifest-c2.txt":
            for line in open(p, encoding="latin-1"):
                t = line.split()
                if len(t) < 3 or t[0].startswith("#"):
                    continue
                specs = []
                if t[1] in ("run", "kill", "refuse", "emit"):
                    specs = [t[2]]
                elif t[1] == "same" and len(t) >= 4:
                    specs = [t[2], t[3]]
                for s in specs:
                    data = qual_src(s)
                    if data is not None and not is_om2(data):
                        add(data, "qual:%s:%s" % (rel, t[0]))

def compile_(data):
    p = subprocess.run([OMC], input=data, capture_output=True, timeout=120)
    return p.returncode, p.stdout, p.stderr

def run_of(data, u):
    """(start, end) of the longest stretch data[start:end] == u * k found from u*256 seeds"""
    best = (0, 0)
    seed = u * 256
    s = data.find(seed)
    while s >= 0:
        e = s
        while data.startswith(u, e):
            e += len(u)
        b = s
        while b >= len(u) and data[b - len(u):b] == u:
            b -= len(u)
        if e - b > best[1] - best[0]:
            best = (b, e)
        s = data.find(seed, e)
    return best

def segments(data):
    """list of ('pack', bytes) / ('cyc', path, n) whose concatenation is data"""
    if len(data) <= 1 << 20:
        return [("pack", data)]
    cands = []
    for k, (path, u) in CYC.items():
        b, e = run_of(data, u)
        cands.append((e - b, b, e, path, u))
    n, b, e, path, u = max(cands)
    assert n * 10 >= len(data) * 9, "big input without a dominant repeated unit"
    tail = data[e:]
    k = n
    # extend the cyc segment over a partial unit at the end (e.g. a cut "note fi")
    p = 0
    while p < len(tail) and p < len(u) and tail[p] == u[p]:
        p += 1
    segs = []
    if b:
        segs.append(("pack", data[:b]))
    segs.append(("cyc", path, k + p))
    if tail[p:]:
        segs.append(("pack", tail[p:]))
    assert all(len(s[1]) <= 1 << 20 for s in segs if s[0] == "pack")
    return segs

pack = bytearray()
manifest, index = [], []
stderr_forms = {}
for i, h in enumerate(order, 1):
    data = units[h]
    rc, out, err = compile_(data)
    cid = "c2-%05d" % i
    parts = []
    for s in segments(data):
        if s[0] == "cyc":
            parts.append("cyc:%s:%d" % (s[1], s[2]))
        else:
            hdr = ("\n#### %s sha256=%s len=%d\n" % (cid, h, len(data))).encode()
            pack += hdr
            parts.append("pack:%s:%d:%d" % (PACKREL, len(pack), len(s[1])))
            pack += s[1]
    src = "+".join(parts)
    so = "sha:" + sha(out) if out else "-"
    m = re.fullmatch(rb"omc: line (\d+): error (\d+)\n", err)
    if not err:
        se = "-"
    elif m and int(m.group(2)) == rc:
        se = "err:%s:%s" % (m.group(1).decode(), m.group(2).decode())
    else:
        p = "seed/qualification/om2/c2/stderr-%s.txt" % sha(err)[:16]
        stderr_forms[p] = err
        se = "file:" + p
    manifest.append("%s emit %s %s %s %d" % (cid, src, so, se, rc))
    index.append("\t".join([cid, h, str(len(data)), str(rc), sha(out) if out else "-",
                            err.decode("latin-1").rstrip("\n").replace("\t", " ") or "-",
                            ",".join(origins[h])]))

os.makedirs(os.path.join(OUT, "c2"), exist_ok=True)
open(os.path.join(OUT, "c2/corpus.pack"), "wb").write(bytes(pack))
for k, (path, pat) in CYC.items():
    open(os.path.join(OUT, path.split("seed/qualification/om2/")[1]), "wb").write(pat)
for p, e in stderr_forms.items():
    open(os.path.join(OUT, p.split("seed/qualification/om2/")[1]), "wb").write(e)
hdr = [
    "# OM-2 C2 manifest (contract spec/omegese-om2.md section 0, C2), frozen by the #748 lane (L836).",
    "# Every entry is an OM-1 unit compiled by the pinned omc %s (main acb3f15)" % OMC_PIN,
    "# and its result under that compiler: stdout sha256 (sha:) or empty (-), the exact stderr",
    "# (err:LINE:CODE = \"omc: line LINE: error CODE\"), and the exit status. A candidate OM-2 compiler",
    "# passes C2 only if every entry PASSes: build/om2-qual/gate CANDIDATE SHA manifest-c2.txt SCRATCH.",
    "# Sources are pinned by bytes: pack:PATH:OFFSET:LENGTH slices of c2/corpus.pack, or cyc:PATH:N",
    "# (N bytes repeating PATH). c2/index.tsv lists each entry's source sha256, length, origins.",
    "# Scope (contract C2): every tracked .om/.omegese of acb3f15 except seed/omc/omc.om; every .om of",
    "# omega-bridge 012860db (om/omega-bridge.lock); every input the seed gates gave the compiler",
    "# (seed/rung4/gate4.sh with omc in om4's place as gate5 runs it, seed/omc/gate5.sh inline programs",
    "# and seed/check/nextfuzz.sh 20, gate6-17, gate-ir, seed/host/gate.sh, security673/gate669.om).",
    "# Excluded: omc.om itself (C2), gate5 section 7 prefixes of omc.om, and gate5 error-1/2 I/O cases",
    "# (a directory as input, /dev/full as output), which have no unit bytes. Appended after those: the",
    "# #748 lane's own OM-1 units (qual: origins; every .om and case-list source under",
    "# seed/qualification/om2 that is not OM-2 by its first non-note, non-blank line). Entries=%d." % len(manifest),
    "deadline 120",
]
open(os.path.join(OUT, "manifest-c2.txt"), "w").write("\n".join(hdr + manifest) + "\n")
open(os.path.join(OUT, "c2/index.tsv"), "w").write(
    "id\tsource_sha256\tbytes\texit\tstdout_sha256\tstderr\torigins\n" + "\n".join(index) + "\n")
print("entries", len(manifest), "pack bytes", len(pack), "stderr forms", len(stderr_forms))
