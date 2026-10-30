fn power_mod_u32(a: u32, b: u32) -> u32 {
    let (mut r, mut c, mut e) = (1u32, a, b);
    while e != 0 { if e & 1 == 1 { r = r.wrapping_mul(c); } c = c.wrapping_mul(c); e >>= 1; }
    r
}
