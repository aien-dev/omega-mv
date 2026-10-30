def fibonacci_u32(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, (a + b) & 0xFFFFFFFF
    return a
