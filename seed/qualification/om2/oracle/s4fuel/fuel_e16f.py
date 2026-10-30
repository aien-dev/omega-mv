#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# E14/E16 fuel model, per-function form (oracle, outside the delivered closure; Python 3.12.3).
# Reads a fixture in the subset used by sec/s4fuel (fn NAME ... [inline], `for V T K unroll`, a line that is
# a bare function name = a call, end, note, ret). Every function, inline ones included, is compiled in
# source order with its own fuel 65536 (E14 "per function"; OMEGESE 9.13: an inline function is also
# compiled standalone). E16: an unrolled loop is charged K at its line when it starts; an inline expansion
# 1 at its call line; error 14 names the construct whose charge takes the total above 65536.
# Compilation stops at the first error: the first function, in source order, to exhaust its fuel.
import sys
class Out(Exception): pass

def parse(path):
    fns, order, cur, stack = {}, [], None, []
    for no, raw in enumerate(open(path), 1):
        w = raw.split()
        if not w or w[0] == "note" or w[0] == "om2":
            continue
        if w[0] == "fn":
            cur = w[1]; fns[cur] = {"inline": w[-1] == "inline", "body": []}; order.append(cur)
            stack = [fns[cur]["body"]]
        elif w[0] == "for" and w[-1] == "unroll":
            node = ("loop", no, int(w[3]), []); stack[-1].append(node); stack.append(node[3])
        elif w[0] == "end":
            stack.pop()
        elif len(w) == 1 and w[0] in fns or (len(w) == 1 and w[0] not in ("ret",)):
            stack[-1].append(("call", no, w[0]))
    return fns, order

def compile_fn(fns, name):
    used = [0]
    def charge(n, line):
        used[0] += n
        if used[0] > 65536:
            raise Out(line)
    def walk(body):
        for it in body:
            if it[0] == "loop":
                charge(it[2], it[1])
                for _ in range(it[2]):
                    walk(it[3])
            elif fns.get(it[2], {}).get("inline"):
                charge(1, it[1]); walk(fns[it[2]]["body"])
    walk(fns[name]["body"])
    return used[0]

fns, order = parse(sys.argv[1])
for f in order:
    try:
        n = compile_fn(fns, f)
        print("fn %s total %d" % (f, n))
    except Out as e:
        print("fn %s exhausted: error 14 at line %d" % (f, e.args[0]))
        break
