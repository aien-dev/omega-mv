def hamming_dist_u32(a, b):
    return bin(a ^ b).count("1")
