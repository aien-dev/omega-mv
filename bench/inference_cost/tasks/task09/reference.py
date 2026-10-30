def factorial_u32(n):
    if n >= 34:
        return 0
    r = 1
    for i in range(2, n + 1):
        r = (r * i) & 0xFFFFFFFF
    return r
