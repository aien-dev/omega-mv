fn popcount_u32(x: u32) -> u32 {
    let mut v = x;
    let mut c = 0u32;
    while v != 0 { c += v & 1; v >>= 1; }
    c
}
