fn sum_digits_u32(n: u32) -> u32 {
    let (mut v, mut s) = (n, 0u32);
    while v != 0 { s += v % 10; v /= 10; }
    s
}
