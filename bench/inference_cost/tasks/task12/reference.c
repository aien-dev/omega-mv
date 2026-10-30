uint32_t clz_u32(uint32_t x) {
    uint32_t c = 0;
    for (uint32_t m = 0x80000000u; m && !(x & m); m >>= 1) c++;
    return c;
}
