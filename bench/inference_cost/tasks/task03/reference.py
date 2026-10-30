def popcount_u32(x):
    c = 0
    while x:
        c += x & 1
        x >>= 1
    return c
