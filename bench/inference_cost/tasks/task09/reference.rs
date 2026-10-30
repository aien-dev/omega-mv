fn factorial_u32(n: u32) -> u32 {
    if n >= 34 { return 0; }
    let mut r = 1u32;
    for i in 2..=n { r = r.wrapping_mul(i); }
    r
}
