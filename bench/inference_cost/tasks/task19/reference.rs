fn hamming_dist_u32(a: u32, b: u32) -> u32 {
    (a ^ b).count_ones()
}
