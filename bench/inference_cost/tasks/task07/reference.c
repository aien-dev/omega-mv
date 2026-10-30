uint32_t bitrev_u16(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 16; i++) { r = (r << 1) | (x & 1u); x >>= 1; }
    return r;
}
