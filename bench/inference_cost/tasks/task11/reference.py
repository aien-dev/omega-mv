def parity_u32(x):
    p = 0
    while x:
        p ^= x & 1
        x >>= 1
    return p
