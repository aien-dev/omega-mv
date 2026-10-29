fn isqrt_u32(x: u32) -> u32 {
    let (mut lo, mut hi, mut ans) = (0u64, 65535u64, 0u64);
    while lo <= hi {
        let mid = (lo + hi) / 2;
        if mid * mid <= x as u64 { ans = mid; lo = mid + 1; } else { hi = mid - 1; }
    }
    ans as u32
}
