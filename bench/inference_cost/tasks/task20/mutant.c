uint32_t rot_left_u32(uint32_t x, uint32_t k) {
    return x << (k % 32u);
}
