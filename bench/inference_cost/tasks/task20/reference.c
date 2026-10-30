uint32_t rot_left_u32(uint32_t x, uint32_t k) {
    uint32_t s = k % 32u;
    return s == 0 ? x : (x << s) | (x >> (32u - s));
}
