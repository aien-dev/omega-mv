uint32_t ctz_u32(uint32_t x) {
    if (x == 0) return 32;
    uint32_t c = 0;
    while (!(x & 1u)) { c++; x >>= 1; }
    return c;
}
