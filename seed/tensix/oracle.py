#!/usr/bin/env python3
# oracle.py -- interop oracle, not Omega-native; never generates kernels.
#
# Host dependency (oracle only): Python 3 standard library (ctypes, struct). It moves bytes that asm2 made
# into two outside checkers and moves the result back out; it never writes, changes or chooses an
# instruction word. Omega replacement: an .om host loader once Omega has a dlopen/FFI word (ttsim path)
# and a byte-writer for ELF headers (qemu path); both are proposals, not implemented.
#
#   oracle.py ttsim LIB ARCH IMAGE A B N OUT
#       Tenstorrent ttsim (libttsim_{wh,bh}), the vendor's open simulator, driven through its PCI Express
#       interface only, the way a host driver drives a card: read BAR0 from config space, point one TLB
#       window at Tensix tile L1, hold the tile's five RISC-V cores in soft reset, write IMAGE at L1 0x0
#       (the reset pc of RISCV B), the argument block at 0x1000, A at 0x2000, B at 0x3000, then release
#       RISCV B only and clock until the stub writes the done mark 0x0600d000 at 0x1010. OUT gets the 4N
#       bytes at 0x4000. ARCH is wh (Wormhole B0, n150) or bh (Blackhole A0, p150).
#   oracle.py elf IMAGE A B N OUT
#       Wraps IMAGE in a 32-bit RISC-V ELF executable for qemu-riscv32 (Linux user mode): one load segment
#       at 0x10000, IMAGE at 0x10100 (the entry), argument block 0x11000, A 0x12000, B 0x13000, out 0x14000.
# Sources (read 2026-10-26): tt-isa-documentation be833101 WormholeB0/PCIExpressTile/TLBs.md,
# BlackholeA0/PCIExpressTile/HostToDeviceTLBs.md, */TensixTile/SoftReset.md, */NoC/Coordinates.md;
# ttsim b3620244 src/libttsim.cpp (API), src/pcie.cpp (config space); ELF: System V gABI, RISC-V psABI.
import ctypes, struct, sys

L1_ARGS, L1_A, L1_B, L1_OUT, DONE = 0x1000, 0x2000, 0x3000, 0x4000, 0x0600D000
SOFT_RESET_0 = 0xFFB121B0           # RISCV_DEBUG_REG_SOFT_RESET_0 (BH SoftReset.md; WH tile_regs 0xFFB12000+0x1B0)
ALL_CORES, BRISC = 0x47800, 0x800   # bits 11 (B), 12-14 (T0-T2), 18 (NC)
TLB_CFG = 0x1FC00000                # TLB configuration array in BAR0 (both architectures)


def die(msg):
    sys.stderr.write("oracle: " + msg + "\n")
    sys.exit(1)


def words(path, n):
    data = open(path, "rb").read()
    if len(data) != 4 * n:
        die("%s has %d bytes, want %d" % (path, len(data), 4 * n))
    return data


def ttsim(lib_path, arch, image_path, a_path, b_path, n, out_path):
    lib = ctypes.CDLL(lib_path)
    u32, u64, vp = ctypes.c_uint32, ctypes.c_uint64, ctypes.c_void_p
    lib.libttsim_pci_config_rd32.restype = u32
    lib.libttsim_pci_config_rd32.argtypes = [u32, u32]
    lib.libttsim_pci_mem_wr_bytes.argtypes = [u64, vp, u32]
    lib.libttsim_pci_mem_rd_bytes.argtypes = [u64, vp, u32]
    lib.libttsim_clock.argtypes = [u32]
    lib.libttsim_init()
    ident = lib.libttsim_pci_config_rd32(0, 0x0)
    want = {"wh": 0x401E1E52, "bh": 0xB1401E52}[arch]
    if ident != want:
        die("config space id 0x%08x, want 0x%08x for %s" % (ident, want, arch))
    if not lib.libttsim_pci_config_rd32(0, 0x4) & 2:
        die("BAR memory decode (Command.MSE) is off")
    bar0 = (lib.libttsim_pci_config_rd32(0, 0x10) & ~0xF) | (lib.libttsim_pci_config_rd32(0, 0x14) << 32)

    def wr(addr, data):
        buf = ctypes.create_string_buffer(data, len(data))
        lib.libttsim_pci_mem_wr_bytes(bar0 + addr, buf, len(data))

    def rd(addr, size):
        buf = ctypes.create_string_buffer(size)
        lib.libttsim_pci_mem_rd_bytes(bar0 + addr, buf, size)
        return buf.raw

    if arch == "wh":
        # 1 MiB windows: local_offset = top 16 bits of the 36-bit tile address, x_end at bit 16, y_end at 22.
        x, y, shift = 1, 1, 20       # NoC #0 (1,1) is a Tensix tile (WormholeB0/NoC/Coordinates.md)

        def point(window, tile_addr):
            cfg = (tile_addr >> shift) | (x << 16) | (y << 22)
            wr(TLB_CFG + 8 * window, struct.pack("<Q", cfg))
            return window << shift
    else:
        # 2 MiB windows, 96-bit configuration: local_offset = tile address >> 21, x_end at bit 43, y_end at 49.
        x, y, shift = 1, 2, 21       # NoC #0 (1,2) is a Tensix tile (BlackholeA0/NoC/Coordinates.md)

        def point(window, tile_addr):
            cfg = (tile_addr >> shift) | (x << 43) | (y << 49)
            for i in range(3):                                   # three 32-bit register writes
                wr(TLB_CFG + 12 * window + 4 * i, struct.pack("<I", (cfg >> (32 * i)) & 0xFFFFFFFF))
            return window << shift

    l1 = point(0, 0)
    regs = point(1, SOFT_RESET_0 & ~((1 << shift) - 1)) + (SOFT_RESET_0 & ((1 << shift) - 1))
    reset_before = struct.unpack("<I", rd(regs, 4))[0]
    wr(regs, struct.pack("<I", ALL_CORES))                       # hold all five cores
    image = open(image_path, "rb").read()
    wr(l1 + 0, image)
    wr(l1 + L1_ARGS, struct.pack("<IIIII", L1_A, L1_B, L1_OUT, n, 0))
    wr(l1 + L1_A, words(a_path, n))
    wr(l1 + L1_B, words(b_path, n))
    wr(l1 + L1_OUT, b"\xee" * (4 * n))                         # poison: an unrun kernel cannot pass
    wr(regs, struct.pack("<I", ALL_CORES & ~BRISC))            # release RISCV B only
    clocks, step, limit = 0, 100, 200000
    while struct.unpack("<I", rd(l1 + L1_ARGS + 16, 4))[0] != DONE:
        if clocks >= limit:
            die("no done mark after %d clocks" % clocks)
        lib.libttsim_clock(step)
        clocks += step
    open(out_path, "wb").write(rd(l1 + L1_OUT, 4 * n))
    wr(regs, struct.pack("<I", ALL_CORES))
    lib.libttsim_exit()
    print("ttsim %s: tile (%d,%d) soft_reset_0 at start 0x%x, image %d bytes, done within %d clocks"
          % (arch, x, y, reset_before, len(image), clocks))


def elf(image_path, a_path, b_path, n, out_path):
    base, code, args, a, b, out = 0x10000, 0x100, 0x1000, 0x2000, 0x3000, 0x4000
    image = open(image_path, "rb").read()
    if code + len(image) > args:
        die("image too large")
    body = bytearray(out + 4 * n)
    body[code:code + len(image)] = image
    body[args:args + 16] = struct.pack("<IIII", base + a, base + b, base + out, n)
    body[a:a + 4 * n] = words(a_path, n)
    body[b:b + 4 * n] = words(b_path, n)
    # ELF32 header: little-endian, ET_EXEC, EM_RISCV (243), version 1, entry, phoff 52, no sections,
    # e_flags 0 (soft-float ABI, no RVC), ehsize 52, one 32-byte program header.
    ehdr = b"\x7fELF\x01\x01\x01" + bytes(9) + struct.pack(
        "<HHIIIIIHHHHHH", 2, 243, 1, base + code, 52, 0, 0, 52, 32, 1, 0, 0, 0)
    # PT_LOAD, offset 0 -> vaddr 0x10000, read/write/execute, page aligned.
    phdr = struct.pack("<IIIIIIII", 1, 0, base, base, len(body), len(body), 7, 0x1000)
    body[0:84] = ehdr + phdr
    open(out_path, "wb").write(bytes(body))


if __name__ == "__main__":
    a = sys.argv[1:]
    if len(a) == 8 and a[0] == "ttsim" and a[2] in ("wh", "bh"):
        ttsim(a[1], a[2], a[3], a[4], a[5], int(a[6]), a[7])
    elif len(a) == 6 and a[0] == "elf":
        elf(a[1], a[2], a[3], int(a[4]), a[5])
    else:
        die("usage: oracle.py ttsim LIB wh|bh IMAGE A B N OUT | oracle.py elf IMAGE A B N OUT")
