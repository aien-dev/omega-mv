# Compare bc-derived K/H (kiv.out) with the hex tables extracted by pdftotext from NIST.FIPS.180-4.pdf.
import re, sys
t = open(sys.argv[1]).read().split("\n")
k = " ".join(t[585:593]).split()          # lines 586-593: section 4.2.2 SHA-256 K
h = [re.search(r"= ([0-9a-f]{8})\b", t[i].strip()).group(1) for i in range(729, 737)]  # 5.3.3
assert len(k) == 64 and all(re.fullmatch(r"[0-9a-f]{8}", x) for x in k)
got = [l.split() for l in open(sys.argv[2])]
assert [int(x, 16) for x in k] == [int(g[3]) for g in got if g[0] == 'K']
assert [int(x, 16) for x in h] == [int(g[3]) for g in got if g[0] == 'H']
print("PASS bc K[0..63] and H(0)[0..7] equal FIPS 180-4 sections 4.2.2 and 5.3.3")
