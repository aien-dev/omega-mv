def sum_digits_u32(n):
    s = 0
    while n:
        s += n % 10
        n //= 10
    return s
