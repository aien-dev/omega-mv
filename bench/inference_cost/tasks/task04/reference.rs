fn gcd_u32(a: u32, b: u32) -> u32 {
    let (mut x, mut y) = (a, b);
    while y != 0 { let t = x % y; x = y; y = t; }
    x
}
