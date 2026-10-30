uint32_t collatz_steps_u32(uint32_t n) {
    uint64_t c = n;
    uint32_t s = 0;
    while (c > 1) { c = (c & 1u) ? 3 * c + 1 : c / 2; s++; }
    return s;
}
