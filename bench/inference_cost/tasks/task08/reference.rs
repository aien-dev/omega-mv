fn fibonacci_u32(n: u32) -> u32 {
    let (mut a, mut b) = (0u32, 1u32);
    for _ in 0..n { let c = a.wrapping_add(b); a = b; b = c; }
    a
}
