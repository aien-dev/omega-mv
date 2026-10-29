uint32_t isqrt_u32(uint32_t x) {
    uint64_t lo = 0, hi = 65535, ans = 0;
    while (lo <= hi) {
        uint64_t mid = (lo + hi) / 2;
        if (mid * mid <= (uint64_t)x) { ans = mid; lo = mid + 1; } else { hi = mid - 1; }
    }
    return (uint32_t)ans;
}
