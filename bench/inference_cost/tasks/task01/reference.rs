fn clamp_add_u16(a: u32, b: u32) -> u32 {
    let s = a.wrapping_add(b);
    if s > 65535 { 65535 } else { s }
}
