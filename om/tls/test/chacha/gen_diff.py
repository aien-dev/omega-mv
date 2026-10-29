#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# gen_diff.py: test tooling only. A seeded random differential corpus for driver.om, with expected
# values from the python `cryptography` package (OpenSSL), an implementation independent of ours.
# Lengths cover 0..300 bytes (every block-boundary residue), random keys, nonces, counters, aad.
# Usage: python3 gen_diff.py SEED COUNT > OUT
import random
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.poly1305 import Poly1305

seed, count = int(sys.argv[1]), int(sys.argv[2])
rng = random.Random(seed)


def rb(n):
    return bytes(rng.getrandbits(8) for _ in range(n))


def hx(b):
    return b.hex() or "-"


print("# gen_diff.py seed %d count %d (oracle: python cryptography)" % (seed, count))
for i in range(count):
    n = i % 301
    key, nonce, msg = rb(32), rb(12), rb(n)
    nb = (n + 63) // 64
    ctr = rng.choice([0, 1, rng.getrandbits(32) % (0x100000001 - nb), 0x100000000 - nb if nb else 0xffffffff])
    ks = Cipher(algorithms.ChaCha20(key, ctr.to_bytes(4, "little") + nonce), None).encryptor()
    print("C %s %d %s %s 0" % (hx(key + nonce), ctr, hx(msg), hx(ks.update(msg))))
    print("P %s %s %s 0" % (hx(key), hx(msg), hx(Poly1305.generate_tag(key, msg))))
    aad = rb(rng.choice([0, 1, 12, 15, 16, 17, n]))
    print("A %s %s %s %s 0" % (hx(key + nonce), hx(aad), hx(msg),
                                hx(ChaCha20Poly1305(key).encrypt(nonce, msg, aad))))
