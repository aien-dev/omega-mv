uint32_t is_prime_u32(uint32_t n) {
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (uint64_t d = 3; d * d <= n; d += 2) if (n % d == 0) return 0;
    return 1;
}
