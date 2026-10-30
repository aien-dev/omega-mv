fn bitrev_u16(x: u32) -> u32 {
    let mut v = x;
    let mut r = 0u32;
    for _ in 0..16 { r = (r << 1) | (v & 1); v >>= 1; }
    r
}
