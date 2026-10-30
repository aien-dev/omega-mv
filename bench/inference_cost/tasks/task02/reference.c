uint64_t absdiff_i32(int32_t a, int32_t b) {
    int64_t d = (int64_t)a - (int64_t)b;
    return d < 0 ? (uint64_t)(-d) : (uint64_t)d;
}
