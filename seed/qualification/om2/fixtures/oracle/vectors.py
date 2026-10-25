# EXTERNAL ORACLE RECIPE (outside the delivered closure; not first-party automation).
# Reproduces seed/qualification/om2/fixtures/sha256-vectors.txt, fixtures/msg/*.bin and
# fixtures/gen/*.bin. Primary digest oracle: /usr/bin/sha256sum (GNU coreutils). Each digest is
# cross-checked against Python hashlib, and the three SHA-256 digests NIST publishes in FIPS 180-2
# Appendix B are checked against both. Messages are extracted from NIST text (pdftotext output),
# never typed. Usage, see ../PROVENANCE.txt:
#   python3 vectors.py SHA256.txt SHA512.txt fips180-2.txt FIXTURE_DIR
import hashlib, os, re, subprocess, sys

sha256_txt, sha512_txt, fips2_txt, out = sys.argv[1:5]

def sha256sum(data):
    r = subprocess.run(["/usr/bin/sha256sum"], input=data, capture_output=True, check=True)
    d = r.stdout.decode().split()[0]
    assert d == hashlib.sha256(data).hexdigest(), "sha256sum and hashlib disagree"
    return d

# Generators, byte for byte the same as gate.om: seq:N byte i = i mod 251; rnd:SEED:N splitmix64.
def seq(n):
    return bytes(i % 251 for i in range(n))
M64 = (1 << 64) - 1
def splitmix_bytes(seed, n):
    s, outb = seed, bytearray()
    while len(outb) < n:
        s = (s + 0x9E3779B97F4A7C15) & M64
        z = s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        z ^= z >> 31
        outb += z.to_bytes(8, "little")
    return bytes(outb[:n])

t256 = open(sha256_txt).read()
t512 = open(sha512_txt).read()
quoted = re.findall(r'"([a-z]+)"', t256)
assert len(quoted) == 2 and quoted[0] == "abc"
abc, m448 = quoted[0].encode(), quoted[1].encode()
q512 = re.findall(r'"([a-z\n]+)"', t512)
assert len(q512) == 2 and q512[0] == "abc", q512
m896 = q512[1].replace("\n", "").encode()
assert (len(abc), len(m448), len(m896)) == (3, 56, 112), "448-bit and 896-bit message lengths"

lines = open(fips2_txt).read().split("\n")
pub = [l.strip().rstrip(".").replace(" ", "") for l in lines[1507:1887]
       if re.fullmatch(r"\s*([0-9a-f]{8} ){7}[0-9a-f]{8}\.", l)]
assert len(pub) == 3, pub
million = b"a" * 1000000
for msg, want in zip((abc, m448, million), pub):
    assert sha256sum(msg) == want, "published FIPS 180-2 Appendix B digest"

os.makedirs(os.path.join(out, "msg"), exist_ok=True)
os.makedirs(os.path.join(out, "gen"), exist_ok=True)
for name, msg in (("abc", abc), ("m448", m448), ("m896", m896)):
    open(os.path.join(out, "msg", name + ".bin"), "wb").write(msg)

rows = []
def row(name, spec, data):
    rows.append("%s %s %d %s" % (name, spec, len(data), sha256sum(data)))

P = "seed/qualification/om2/fixtures/msg/"
row("fips-empty", "-", b"")
row("fips-abc", "file:" + P + "abc.bin", abc)
row("fips-448", "file:" + P + "m448.bin", m448)
row("fips-896", "file:" + P + "m896.bin", m896)
row("fips-million-a", "rep:97:1000000", million)
for n in list(range(0, 131)) + [1000, 4095, 4096, 4097, 8191, 8192, 8193, 65536]:
    row("seq-%d" % n, "seq:%d" % n, seq(n))
for n in (55, 56, 63, 64, 65, 4096):
    row("rep-a-%d" % n, "rep:97:%d" % n, b"a" * n)
# Random cases: seeds 1..8, lengths chosen to straddle the 55/56 padding edge, block and
# 4096-byte piece boundaries, and one large multi-piece message.
for seed, n in ((1, 3), (2, 55), (3, 56), (4, 64), (5, 111), (6, 4097), (7, 12345), (8, 100003)):
    row("rnd-%d-%d" % (seed, n), "rnd:%d:%d" % (seed, n), splitmix_bytes(seed, n))
open(os.path.join(out, "sha256-vectors.txt"), "w").write("\n".join(rows) + "\n")

# Generator self-test inputs for gate.om (expected stdout of an identity program).
open(os.path.join(out, "gen", "seq-300.bin"), "wb").write(seq(300))
open(os.path.join(out, "gen", "rnd-7-64.bin"), "wb").write(splitmix_bytes(7, 64))
open(os.path.join(out, "gen", "rep-0-5.bin"), "wb").write(b"\0" * 5)
print("PASS %d vectors; FIPS 180-2 B.1, B.2, B.3 published digests matched" % len(rows))
