def bitrev_u16(x):
    r = 0
    for _ in range(16):
        r = (r << 1) | (x & 1)
        x >>= 1
    return r
