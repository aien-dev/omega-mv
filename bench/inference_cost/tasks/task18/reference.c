uint32_t reverse_digits_u32(uint32_t n) {
    uint32_t r = 0;
    while (n) { r = r * 10 + n % 10; n /= 10; }
    return r;
}
