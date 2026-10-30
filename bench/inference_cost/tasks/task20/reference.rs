fn rot_left_u32(x: u32, k: u32) -> u32 {
    x.rotate_left(k % 32)
}
