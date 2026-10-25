# EXTERNAL ORACLE RECIPE (outside the delivered closure).
#   python3 obligations.py xs           x inputs for obligations.bc: 0, 1, 2^31, 2^32-1, then
#                                        eight seeded values (splitmix64 seed 748, high 32 bits)
#   python3 obligations.py check FILE   recompute FILE (bc output) independently; PASS or assert
import sys
M64 = (1 << 64) - 1
def splitmix(seed, count):
    s, out = seed, []
    for _ in range(count):
        s = (s + 0x9E3779B97F4A7C15) & M64
        z = s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        out.append(z ^ (z >> 31))
    return out
XS = [0, 1, 1 << 31, (1 << 32) - 1] + [z >> 32 for z in splitmix(748, 8)]
M = M64
SL = [(M, M - 1, 4), (M, M - 1, 1), (M, M - 1, 2), (M, M, 0), (M, M, 1), (10, 11, 0), (10, M, 2),
      (10, 3, 0), (0, 0, 0), (0, 0, 1), (10, 10, 0), (10, 10, 1), (10, 3, 7), (10, 3, 8),
      (4096, 4095, 1), (4096, 4095, 2)]
if sys.argv[1] == "xs":
    print("\n".join(str(x) for x in XS + [-1]))
    sys.exit(0)
rows = [l.split() for l in open(sys.argv[2])]
dup = [list(map(int, r[1:])) for r in rows if r[0] == "dup32"]
sl = [list(map(int, r[1:])) for r in rows if r[0] == "slice"]
assert [r[0] for r in dup] == XS and len(sl) == len(SL)
for x, d, lo, hi, ok in dup:
    assert d == ((1 << 32) + 1) * x and d <= M64 and lo == d & 0xFFFFFFFF and hi == d >> 32
    assert lo == x and hi == x and ok == 1
for (n, o, k, w, e), (n2, o2, k2) in zip(sl, SL):
    assert (n, o, k) == (n2, o2, k2)
    assert w == int(((o + k) & M64) <= n) and e == int(o <= n and k <= n - o)
assert sl[0][3:] == [1, 0], "issue #748 witness: wrapped (o+k) <= n TRUE, k <= n-o FALSE"
print("PASS %d dup32 rows, %d slice rows; issue witness wrapped=1 exact=0" % (len(dup), len(sl)))
