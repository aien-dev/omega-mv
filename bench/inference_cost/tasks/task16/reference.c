int32_t max3_i32(int32_t a, int32_t b, int32_t c) {
    int32_t m = a;
    if (b > m) m = b;
    if (c > m) m = c;
    return m;
}
