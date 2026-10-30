def clamp_add_u16(a, b):
    s = a + b
    return 65535 if s > 65535 else s
