#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Expected refusal of each OM-2 fixture of sec/s3s4 by the pinned OM-1 compiler 734f0a39, from the
# OM-1 rules, without running any compiler:
#  - OMEGESE 9.2: a names pass reads every fn signature before any code, so a signature that is not
#    OM-1 is refused first, at its line, before the om2 line (a code line) is reached.
#    In a signature, the first non-OM-1 token decides the code:
#      `*` as an array length: error 16 (OMEGESE 9.4: an array parameter without its length);
#      `out` as a parameter kind: error 16 (OMEGESE 9.11: an unknown type name is error 16);
#      a word after the return type (`to T inline`): error 5 (OM-1 "tokens left after the
#      statement" code; contract section 2 line 59-61 says it is pinned by fixtures; the value 5 is
#      the one reported by the QualPhaseB.FixInvariants lane, 2026-10-25, and is checked by the gate).
#  - Otherwise the first non-note line `om2`, `om2 diag` or `om2 fmt` is refused with error 16
#    (contract section 0 lines 13-14).
# Output: "<fixture path> <line> <code>" per OM-2 fixture (first non-note line starts with om2).
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SEC = os.path.normpath(os.path.join(HERE, "..", "..", "sec", "s3s4"))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))


def sig_error(toks):
    # toks: fn NAME (PARAM KIND ...)* [to T [extra]]
    i = 2
    while i < len(toks) and toks[i] != "to":
        name, kind = toks[i], toks[i + 1]
        if kind == "out":
            return 16
        if kind in ("view", "edit"):
            if toks[i + 3] == "*":
                return 16
            i += 4
        else:
            i += 2
    if i < len(toks) and len(toks) > i + 2:
        return 5
    return None


def rule(path):
    lines = open(path).read().split("\n")
    first = next(j for j, l in enumerate(lines) if not l.startswith("note"))
    if not lines[first].startswith("om2"):
        return None
    for j, l in enumerate(lines):
        if l.startswith("fn "):
            c = sig_error(l.split())
            if c is not None:
                return j + 1, c
    return first + 1, 16


out = []
for p in sorted(glob.glob(os.path.join(SEC, "*.om"))):
    r = rule(p)
    if r:
        out.append("%s %d %d" % (os.path.relpath(p, ROOT), r[0], r[1]))
sys.stdout.write("".join(o + "\n" for o in out))
