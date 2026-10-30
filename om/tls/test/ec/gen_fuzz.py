#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only: deterministic malformed-input corpus for om/tls/test/ec/driver.om. Every line is
# a valid Wycheproof/CAVP case with its signature or public key damaged (byte flips, truncation,
# extension, random replacement, DER length/tag bytes changed), so the expected result is a
# refusal (EXP 1). The point is that untrusted input never traps: a trap ends the driver with
# exit 100+N and fails run.sh.
# Usage: python3 gen_fuzz.py VECTOR_DIR OUT_FILE COUNT SEED
import random
import sys


def main():
    vdir, outp, count, seed = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    rnd = random.Random(seed)
    base = []
    for name in ("wycheproof_secp256r1_sha256", "wycheproof_secp256r1_sha256_p1363",
                 "wycheproof_secp384r1_sha384", "wycheproof_secp384r1_sha384_p1363"):
        for line in open("%s/%s.txt" % (vdir, name)):
            f = line.split()
            if line.startswith("#") or f[3] != "0":
                continue
            base.append(f)
    out = ["# gen_fuzz.py seed %d count %d (all EXP 1)" % (seed, count)]
    for i in range(count):
        f = list(rnd.choice(base))
        sig = bytearray(bytes.fromhex(f[6]))
        pub = bytearray(bytes.fromhex(f[5]))
        op = rnd.randrange(8)
        if op == 0:
            sig[rnd.randrange(len(sig))] ^= 1 << rnd.randrange(8)
        elif op == 1:
            sig = sig[:rnd.randrange(len(sig))]
        elif op == 2:
            sig += bytes(rnd.randrange(256) for _ in range(rnd.randrange(1, 8)))
        elif op == 3:
            sig = bytearray(rnd.randrange(256) for _ in range(rnd.randrange(0, 120)))
        elif op == 4:
            # a DER length or tag byte (positions 0, 1, 2, 3 and the s header) set to a random value
            pos = rnd.choice([0, 1, 2, 3, 4 + sig[3] if len(sig) > 3 and 4 + sig[3] < len(sig) else 0])
            old = sig[pos]
            while sig[pos] == old:
                sig[pos] = rnd.randrange(256)
        elif op == 5:
            pub[rnd.randrange(len(pub))] ^= 1 << rnd.randrange(8)
        elif op == 6:
            pub = pub[:rnd.randrange(len(pub))]
        else:
            pub = bytearray(rnd.randrange(256) for _ in range(rnd.choice([0, 1, 33, 49, 65, 97, 130])))
        f[0] = "f-%d-op%d" % (i, op)
        f[3] = "1"
        f[5] = pub.hex() or "-"
        f[6] = sig.hex() or "-"
        out.append(" ".join(f))
    with open(outp, "w") as fo:
        fo.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
