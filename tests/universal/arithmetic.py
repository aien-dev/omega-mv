"""CPython bigint reference and explicitly different masked-u64 reference."""
import sys
mode = sys.argv[1]
assert mode in ('u64', 'bigint')
for line in sys.stdin:
    a, b = map(int, line.split())
    assert 0 <= a < 2**64 and 0 < b < 2**64
    values = (a+b, a-b, a*b, a//b)
    if mode == 'u64':
        values = tuple(v & (2**64-1) for v in values)
    print(*values)
