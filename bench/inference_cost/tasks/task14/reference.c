uint32_t power_mod_u32(uint32_t a, uint32_t b) {
    uint32_t r = 1;
    while (b) { if (b & 1u) r *= a; a *= a; b >>= 1; }
    return r;
}
