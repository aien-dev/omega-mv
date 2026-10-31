#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""unit.py RECEIPT.om NAME: print the function-mode unit that exports NAME.

Copied from om/gpumath/parity/unit.py (aien-dev/omega-mv#751); only the file names differ.

The unit is NAME and every library function it reaches through calls (the part of receipt.om
before the parity-harness marker), in file order, with NAME moved to the end: the seeded compiler
enters a function-mode unit at its last function and the omega-bridge native call ABI v1 binds that
one; the others stay private helpers. Note lines before a kept function travel with it.
"""
import sys

MARK = "note ==== parity harness"


def functions(lib):
    """[(name, first line incl. leading notes, end line)] of the library."""
    out = []
    i = 0
    while i < len(lib):
        w = lib[i].split()
        if w[:1] == ["fn"]:
            j = i
            while j > 0 and lib[j - 1].startswith("note") and not lib[j - 1].startswith("note ----"):
                j -= 1
            depth = 0
            for k in range(i, len(lib)):
                t = lib[k].split()[:1]
                if t and t[0] in ("fn", "if", "while", "for"):
                    depth += 1
                elif t == ["end"]:
                    depth -= 1
                    if depth == 0:
                        break
            else:
                sys.exit("unit.py: %s has no closing end" % w[1])
            out.append((w[1], j, k))
            i = k + 1
        else:
            i += 1
    return out


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, name = sys.argv[1], sys.argv[2]
    lines = open(src).read().split("\n")
    lib = lines[:next(i for i, l in enumerate(lines) if l.startswith(MARK))]
    fns = functions(lib)
    span = {n: (a, b) for n, a, b in fns}
    if name not in span:
        sys.exit("unit.py: no function %s" % name)
    calls = {}
    for n, a, b in fns:
        words = set()
        for l in lib[a:b + 1]:
            if not l.startswith("note"):
                words.update(l.split())
        calls[n] = {m for m in words if m in span and m != n}
    keep = {name}
    todo = [name]
    while todo:
        for m in calls[todo.pop()]:
            if m not in keep:
                keep.add(m)
                todo.append(m)
    order = [n for n, _, _ in fns if n in keep and n != name] + [name]
    parts = []
    for n in order:
        a, b = span[n]
        parts.append("\n".join(lib[a:b + 1]))
    sys.stdout.write("note SPDX-License-Identifier: Apache-2.0\nnote generated from om/receipt/receipt.om by parity/unit.py %s\n\n" % name)
    sys.stdout.write("\n\n".join(parts) + "\n")


if __name__ == "__main__":
    main()
