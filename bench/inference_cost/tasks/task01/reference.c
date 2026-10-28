uint32_t clamp_add_u16(uint32_t a, uint32_t b) {
    uint32_t s = a + b;
    return s > 65535u ? 65535u : s;
}
