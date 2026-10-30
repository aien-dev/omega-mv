def rot_left_u32(x, k):
    s = k % 32
    return ((x << s) | (x >> (32 - s))) & 0xFFFFFFFF
