uint32_t popcount_u32(uint32_t x) {
    uint32_t c = 0;
    while (x) { c += x & 1u; x >>= 1; }
    return c;
}
