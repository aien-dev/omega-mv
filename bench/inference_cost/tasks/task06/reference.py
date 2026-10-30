def collatz_steps_u32(n):
    s = 0
    while n > 1:
        n = 3 * n + 1 if n & 1 else n // 2
        s += 1
    return s
