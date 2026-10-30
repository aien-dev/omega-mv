def rot_left_u32(x, k):
    return (x << k) & 0xFFFFFFFF
