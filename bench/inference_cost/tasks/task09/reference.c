uint32_t factorial_u32(uint32_t n) {
    if (n >= 34) return 0;
    uint32_t r = 1;
    for (uint32_t i = 2; i <= n; i++) r *= i;
    return r;
}
