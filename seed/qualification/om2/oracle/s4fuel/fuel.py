#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# E14 fuel model (oracle, outside the delivered closure; Python 3.12.3). Fuel 65536 per function; each copy
# of an unrolled body and each inline expansion costs 1, charged when that copy/expansion starts
# (pre-order). The unit that would take the total above 65536 exhausts the fuel; its loop or call line is
# the error line. Prints, per fixture, the total cost or the exhausting line.
FUEL = 65536

def run(tree):
    used = [0]
    def walk(node):  # node = (line, count, children) ; count copies, each charged then children walked
        line, count, kids = node
        for _ in range(count):
            used[0] += 1
            if used[0] > FUEL:
                raise StopIteration(line)
            for k in kids:
                walk(k)
    try:
        for t in tree:
            walk(t)
    except StopIteration as e:
        return "exhausted at line %d" % e.value
    return "total %d" % used[0]

def nest(depth, first_line):
    node = None
    for d in reversed(range(depth)):
        node = (first_line + d, 64, [node] if node else [])
    return [node]

print("fuel-d2", run(nest(2, 5)))
print("fuel-d3", run(nest(3, 5)))
print("fuel-d4", run(nest(4, 5)))
# fuel-inl: main (line 21) calls inline f1 once; f1 calls f2 64 times (lines 6..), each f2 calls f3 64
# times, each f3 calls f4 64 times; f4 is empty. Lines are those of fuel-inl.om (see its notes).
def inl():
    f4 = []
    f3 = [(7, 64, [(8, 1, f4)])]
    f2 = [(12, 64, [(13, 1, f3)])]
    f1 = [(17, 64, [(18, 1, f2)])]
    return [(22, 1, f1)]
print("fuel-inl", run(inl()))
