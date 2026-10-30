#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# gen_rfc.py: test tooling only. Converts the test vectors of RFC 8439 (vectors/rfc8439.txt, a
# byte copy of https://www.rfc-editor.org/rfc/rfc8439.txt) into driver records (see driver.om).
# Hex dumps ("000  4c 61 ...", "02 00 ...", "9f:7b:...") are read mechanically from the RFC text.
# Values that the RFC prints inline (keys in "Key = 00:01:..", state matrices, r/s strings) are
# written below and each one is checked to occur, as a hex string, in the RFC section it is cited
# from, with whitespace and colons removed; a value not found aborts the conversion.
# Usage: python3 gen_rfc.py vectors/rfc8439.txt > vectors/rfc8439-vectors.txt
import re
import sys

LINES = open(sys.argv[1], encoding="ascii").read().split("\n")
DUMP = re.compile(r"^\s*\d{3}  ((?:[0-9a-f]{2} ?){1,16})")
PLAIN = re.compile(r"^\s*((?:[0-9A-F]{2} ){15}[0-9A-F]{2})\s*$")
COLON = re.compile(r"^\s*([0-9a-f]{2}(?::[0-9a-f]{2})+):?\s*$")
out = []


def heading(text):
    for i, line in enumerate(LINES):
        if line.startswith(text):
            return i
    sys.exit("heading not found: " + text)


def section(start, stop):
    return LINES[heading(start):heading(stop)]


def norm(lines):
    return re.sub(r"[\s:*]", "", "\n".join(lines)).lower()


def lit(lines, hexstr):
    h = hexstr.replace(" ", "").replace(":", "").lower()
    if h not in norm(lines):
        sys.exit("literal not found in its RFC section: " + hexstr)
    return h


def data(line):
    m = DUMP.match(line)
    if m:
        return m.group(1).split()
    m = PLAIN.match(line)
    if m:
        return m.group(1).lower().split()
    m = COLON.match(line)
    if m:
        return m.group(1).split(":")
    return None


def labelled(lines):
    # the hex bytes under each label line ("Key:", "Tag:", "The ChaCha20 Key", ...); a label seen
    # again starts over, so the caller reads one test vector's lines at a time
    vals, cur = {}, None
    for line in lines:
        s = line.strip()
        if s.startswith("Nir & Langley") or s.startswith("RFC 8439 ") or s.startswith("\f"):
            continue
        d = data(line)
        if d is not None:
            if cur is not None:
                vals[cur] = vals.get(cur, "") + "".join(d)
            continue
        m = re.match(r"^(?:Initial )?Block Counter = (\d+)$", s)
        if m:
            vals["counter"] = m.group(1)
            cur = None
        elif s.endswith(":") or s == "The ChaCha20 Key":
            cur = s.rstrip(":")
            vals[cur] = ""
        elif s:
            cur = cur if cur is not None and s.startswith("=") else None
    return vals


def vectors(lines):
    # split an appendix section at "Test Vector #n"
    starts = [i for i, l in enumerate(lines) if re.match(r"^\s*Test Vector #\d+", l)]
    starts.append(len(lines))
    return [lines[starts[i]:starts[i + 1]] for i in range(len(starts) - 1)]


def words(lines, text):
    # a 4x4 state printed as eight-digit words, checked against the section
    return lit(lines, text)


def rec(*fields):
    out.append(" ".join(f if f != "" else "-" for f in map(str, fields)))


def note(text):
    out.append("# " + text)


def zeros(n):
    return "00" * n


# 2.1.1 quarter round on a, b, c, d
s = section("2.1.1.", "2.2.  ")
st = lit(s, "11111111") + lit(s, "01020304") + lit(s, "9b8d6f43") + lit(s, "01234567") + "0" * 96
ex = lit(s, "ea2a92f4") + lit(s, "cb1cf8ce") + lit(s, "4581472e") + lit(s, "5881c4bb") + "0" * 96
note("RFC 8439 2.1.1 quarter round (a, b, c, d in words 0..3)")
rec("Q", 0, 1, 2, 3, st, ex)

# 2.2.1 QUARTERROUND(2, 7, 8, 13) on a state
s = section("2.2.1.", "2.3.  ")
st = words(s, "879531e0 c5ecf37d 516461b1 c9a62f8a 44c20ef3 3390af7f d9fc690b 2a5f714c"
              " 53372767 b00a5631 974c541a 359e9963 5c971061 3d631689 2098d9d6 91dbd320")
ex = words(s, "879531e0 c5ecf37d bdb886dc c9a62f8a 44c20ef3 3390af7f d9fc690b cfacafd2")
ex2 = words(s, "e46bea80 b00a5631 974c541a 359e9963 5c971061 ccc07c79 2098d9d6 91dbd320")
note("RFC 8439 2.2.1 QUARTERROUND(2, 7, 8, 13)")
rec("Q", 2, 7, 8, 13, st, ex + ex2)

# 2.3.2 block function
s = section("2.3.2.", "2.4.  ")
key = lit(s, "00:01:02:03:04:05:06:07:08:09:0a:0b:0c:0d:0e:0f:10:11:12:13:14:15:16:17:18:19:1a:1b:1c:1d:1e:1f")
nonce = lit(s, "00:00:00:09:00:00:00:4a:00:00:00:00")
v = labelled(s)
note("RFC 8439 2.3.2 block function, counter 1 (serialized block = keystream)")
rec("C", key + nonce, 1, zeros(64), v["Serialized Block"], 0)

# 2.4.2 encryption
s = section("2.4.2.", "2.5.  ")
nonce = lit(s, "00:00:00:00:00:00:00:4a:00:00:00:00")
v = labelled(s)
note("RFC 8439 2.4.2 encryption, initial counter 1")
rec("C", key + nonce, 1, v["Plaintext Sunscreen"], v["Ciphertext Sunscreen"], 0)
note("RFC 8439 2.4.2 keystream")
rec("C", key + nonce, 1, zeros(len(v["Keystream"]) // 2), v["Keystream"], 0)

# 2.5.2 Poly1305
s = section("2.5.2.", "2.6.  ")
pk = lit(s, "85:d6:be:78:57:55:6d:33:7f:44:52:fe:42:d5:06:a8:01:03:80:8a:fb:0d:b2:fd:4a:bf:f6:af:41:49:f5:1b")
tag = lit(s, "a8:06:1d:c1:30:51:36:c6:c2:2b:8b:af:0c:01:27:a9")
v = labelled(s)
note("RFC 8439 2.5.2 Poly1305")
rec("P", pk, v["Message to be Authenticated"], tag, 0)

# 2.6.2 key generation: the first 32 bytes of block 0
s = section("2.6.2.", "2.7.  ")
v = labelled(s)
note("RFC 8439 2.6.2 Poly1305 key generation (block 0, first 32 bytes)")
rec("C", v["Key"] + v["Nonce"], 0, zeros(32), v["Output bytes"], 0)

# 2.8.2 AEAD
s = section("2.8.2.", "3.  Implementation Advice")
v = labelled(s)
tag = lit(s, "1a:e1:0b:59:4f:09:e2:6a:7e:90:2e:cb:d0:60:06:91")
kn = v["Key"] + v["32-bit fixed-common part"] + v["IV"]
note("RFC 8439 2.8.2 AEAD seal and open")
rec("A", kn, v["AAD"], v["Plaintext"], v["Ciphertext"] + tag, 0)
note("RFC 8439 2.8.2 one-time key (block 0)")
rec("C", kn, 0, zeros(32), v["Poly1305 Key"], 0)
note("RFC 8439 2.8.2 keystream from block 1")
rec("C", kn, 1, zeros(len(v["keystream bytes"]) // 2), v["keystream bytes"], 0)
note("RFC 8439 2.8.2 Poly1305 over the AEAD construction")
rec("P", v["Poly1305 Key"], v["AEAD Construction for Poly1305"], tag, 0)

# A.1 block functions
for i, t in enumerate(vectors(section("A.1.  ", "A.2.  ")), 1):
    v = labelled(t)
    note("RFC 8439 A.1 test vector #%d" % i)
    rec("C", v["Key"] + v["Nonce"], v["counter"], zeros(64), v["Keystream"], 0)

# A.2 encryption
for i, t in enumerate(vectors(section("A.2.  ", "A.3.  ")), 1):
    v = labelled(t)
    note("RFC 8439 A.2 test vector #%d" % i)
    rec("C", v["Key"] + v["Nonce"], v["counter"], v["Plaintext"], v["Ciphertext"], 0)

# A.3 Poly1305
for i, t in enumerate(vectors(section("A.3.  ", "A.4.  ")), 1):
    v = labelled(t)
    note("RFC 8439 A.3 test vector #%d" % i)
    if "One-time Poly1305 Key" in v:
        rec("P", v["One-time Poly1305 Key"], v["Text to MAC"], v["Tag"], 0)
    else:
        rec("P", v["R"] + v["S"], v["data"], v["tag"], 0)

# A.4 key generation
for i, t in enumerate(vectors(section("A.4.  ", "A.5.  ")), 1):
    v = labelled(t)
    note("RFC 8439 A.4 test vector #%d" % i)
    rec("C", v["The ChaCha20 Key"] + v["The nonce"], 0, zeros(32), v["Poly1305 one-time key"], 0)

# A.5 AEAD decryption
s = section("A.5.  ", "Appendix B.")
v = labelled(s)
kn = v["The ChaCha20 Key"] + v["The nonce"]
note("RFC 8439 A.5 AEAD decryption (and seal of the plaintext back to the same ciphertext)")
rec("A", kn, v["The AAD"], v["Plaintext"], v["Ciphertext"] + v["Received Tag"], 0)
note("RFC 8439 A.5 one-time key")
rec("C", kn, 0, zeros(32), v["Poly1305 one-time key"], 0)
note("RFC 8439 A.5 Poly1305 over the AEAD buffer")
rec("P", v["Poly1305 one-time key"], v["Poly1305 Input"], v["Calculated Tag"], 0)

sizes = {"A.5 out bytes": v["out bytes"][:64] == v["Poly1305 one-time key"]}
if not all(sizes.values()):
    sys.exit("A.5 out bytes disagree with the one-time key")
print("# Converted by gen_rfc.py from vectors/rfc8439.txt (RFC 8439, https://www.rfc-editor.org/rfc/rfc8439.txt).")
print("# Fields: see driver.om. %d records." % sum(1 for l in out if not l.startswith("#")))
print("\n".join(out))
