#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# E16 fuel model (oracle, outside the delivered closure; Python 3.12.3). Fuel 65536 per function. An
# unrolled loop is charged K when it starts, at its own line, before any copy expands; an inline expansion
# is charged 1 when it starts, at the call line. Error 14 names the construct whose charge exceeds the fuel
# (total > 65536; a total of exactly 65536 is still within the fuel).
FUEL = 65536
class Out(Exception): pass

def run(items):
    used = [0]
    def charge(n, line):
        used[0] += n
        if used[0] > FUEL:
            raise Out(line)
    def walk(it):
        kind, line, k, body = it
        if kind == "loop":
            charge(k, line)
            for _ in range(k):
                for b in body: walk(b)
        else:  # inline call
            charge(1, line)
            for b in body: walk(b)
    try:
        for it in items: walk(it)
    except Out as e:
        return "exhausted at line %d (used %d)" % (e.args[0], used[0])
    return "total %d" % used[0]

def nest(depth, first):
    node = []
    for d in reversed(range(depth)):
        node = [("loop", first + d, 64, node)]
    return node

print("fuel-d2", run(nest(2, 5)))
print("fuel-d3", run(nest(3, 5)))
print("fuel-d4", run(nest(4, 5)))
f4 = []
f3 = [("loop", 7, 64, [("call", 8, 0, f4)])]
f2 = [("loop", 12, 64, [("call", 13, 0, f3)])]
f1 = [("loop", 17, 64, [("call", 18, 0, f2)])]
print("fuel-inl", run([("call", 22, 0, f1)]))
