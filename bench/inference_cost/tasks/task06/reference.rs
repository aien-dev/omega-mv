fn collatz_steps_u32(n: u32) -> u32 {
    let mut c = n as u64;
    let mut s = 0u32;
    while c > 1 { c = if c & 1 == 1 { 3 * c + 1 } else { c / 2 }; s += 1; }
    s
}
