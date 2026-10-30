def ctz_u32(x):
    if x == 0:
        return 32
    c = 0
    while not x & 1:
        c += 1
        x >>= 1
    return c
