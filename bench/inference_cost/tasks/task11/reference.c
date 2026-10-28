uint32_t parity_u32(uint32_t x) {
    uint32_t p = 0;
    while (x) { p ^= x & 1u; x >>= 1; }
    return p;
}
