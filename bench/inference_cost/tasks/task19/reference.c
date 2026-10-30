uint32_t hamming_dist_u32(uint32_t a, uint32_t b) {
    uint32_t d = a ^ b, c = 0;
    while (d) { c += d & 1u; d >>= 1; }
    return c;
}
