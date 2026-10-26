# Independent cross-check of kiv.out (bc): exact integers (Newton) and decimal at 120 digits.
import sys
from decimal import Decimal, getcontext, ROUND_FLOOR
getcontext().prec = 120
def primes(n):
    out, p = [], 2
    while len(out) < n:
        if all(p % d for d in range(2, int(p**0.5) + 1)): out.append(p)
        p += 1
    return out
def iroot(n, k):
    x = 1 << ((n.bit_length() + k - 1) // k)
    while True:
        y = ((k - 1) * x + n // x ** (k - 1)) // k
        if y >= x: return x
        x = y
P = primes(64)
K = [iroot(p << 96, 3) % 2**32 for p in P]
H = [iroot(p << 64, 2) % 2**32 for p in P[:8]]
def dfrac(p, k):
    r = Decimal(p) ** (Decimal(1) / Decimal(k))
    f = r - r.to_integral_value(rounding=ROUND_FLOOR)
    return int((f * (Decimal(2) ** 32)).to_integral_value(rounding=ROUND_FLOOR))
assert K == [dfrac(p, 3) for p in P] and H == [dfrac(p, 2) for p in P[:8]]
got = [l.split() for l in open(sys.argv[1])]
assert [int(g[3]) for g in got if g[0] == 'K'] == K and [int(g[3]) for g in got if g[0] == 'H'] == H
assert [int(g[2]) for g in got if g[0] == 'K'] == P
print("PASS bc K/H equal python exact-integer and decimal(120) derivations")
