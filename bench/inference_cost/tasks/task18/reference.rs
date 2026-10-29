fn reverse_digits_u32(n: u32) -> u32 {
    let (mut v, mut r) = (n, 0u32);
    while v != 0 { r = r.wrapping_mul(10).wrapping_add(v % 10); v /= 10; }
    r
}
