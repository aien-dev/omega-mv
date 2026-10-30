def gcd_u32(a, b):
    while b:
        a, b = b, a % b
    return a
