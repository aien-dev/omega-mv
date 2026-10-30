uint32_t sum_digits_u32(uint32_t n) {
    uint32_t s = 0;
    while (n) { s += n % 10; n /= 10; }
    return s;
}
