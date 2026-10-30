fn is_prime_u32(n: u32) -> u32 {
    if n < 2 { return 0; }
    if n % 2 == 0 { return if n == 2 { 1 } else { 0 }; }
    let mut d = 3u64;
    while d * d <= n as u64 { if (n as u64) % d == 0 { return 0; } d += 2; }
    1
}
