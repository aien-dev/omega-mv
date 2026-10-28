uint32_t fibonacci_u32(uint32_t n) {
    uint32_t a = 0, b = 1;
    for (uint32_t i = 0; i < n; i++) { uint32_t c = a + b; a = b; b = c; }
    return a;
}
