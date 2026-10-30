#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only: extract the RFC 8448 section 3 (Simple 1-RTT Handshake) values that the
# key-schedule and record KAT (kat.om) checks, as "NAME HEX" lines.
# usage: gen_rfc8448.py vectors/rfc8448.txt > vectors/rfc8448-1rtt.txt
import re, sys, hashlib

raw = open(sys.argv[1], "rb").read()
text = raw.decode()
lines = text.splitlines()
start = next(i for i, l in enumerate(lines) if l.startswith("3.  Simple 1-RTT Handshake"))
end = next(i for i, l in enumerate(lines) if l.startswith("4.  Resumed 0-RTT Handshake"))
body = [l for l in lines[start:end] if not l.startswith("RFC 8448") and not l.startswith("Thomson ")
        and "\f" not in l]

# (header substring, field label, output name[, occurrence of the header, default 0])
want = [
    ("{client}  create an ephemeral x25519 key pair", "private key", "cpriv"),
    ("{server}  create an ephemeral x25519 key pair", "public key", "spub"),
    ("{client}  construct a ClientHello", "ClientHello", "ch"),
    ("{server}  construct a ServerHello", "ServerHello", "sh"),
    ('{server}  extract secret "handshake"', "IKM", "ecdhe"),
    ('{server}  extract secret "handshake"', "secret", "hs"),
    ('{server}  derive secret "tls13 c hs traffic"', "expanded", "chts"),
    ('{server}  derive secret "tls13 s hs traffic"', "expanded", "shts"),
    ('{server}  extract secret "master"', "secret", "ms"),
    ("{server}  derive write traffic keys for handshake data", "key expanded", "shkey"),
    ("{server}  derive write traffic keys for handshake data", "iv expanded", "shiv"),
    ("{server}  derive read traffic keys for handshake data", "key expanded", "chkey"),
    ("{server}  derive read traffic keys for handshake data", "iv expanded", "chiv"),
    ("{server}  construct an EncryptedExtensions", "EncryptedExtensions", "ee"),
    ("{server}  construct a Certificate handshake", "Certificate", "cert"),
    ("{server}  construct a CertificateVerify", "CertificateVerify", "cv"),
    ("{server}  construct a Finished", "Finished", "sfin"),
    ("{server}  send handshake record", "payload", "sflight", 1),
    ("{server}  send handshake record", "complete record", "sflightrec", 1),
    ('{server}  derive secret "tls13 c ap traffic"', "expanded", "cats"),
    ('{server}  derive secret "tls13 s ap traffic"', "expanded", "sats"),
    ("{server}  derive write traffic keys for application data", "key expanded", "sakey"),
    ("{server}  derive write traffic keys for application data", "iv expanded", "saiv"),
    ("{client}  construct a Finished", "Finished", "cfin"),
    ("{client}  send handshake record", "complete record", "cfinrec", 1),
    ("{client}  derive write traffic keys for application data", "key expanded", "cakey"),
    ("{client}  derive write traffic keys for application data", "iv expanded", "caiv"),
    ("{server}  construct a NewSessionTicket", "NewSessionTicket", "nst"),
    ("{client}  send application_data record", "payload", "cdata"),
    ("{client}  send application_data record", "complete record", "cdatarec"),
    ("{server}  send application_data record", "payload", "sdata"),
    ("{server}  send application_data record", "complete record", "sdatarec"),
]

def field(hidx, label):
    i = hidx + 1
    while i < len(body) and not body[i].startswith("   {"):
        m = re.match(r"^\s+" + re.escape(label) + r" \((\d+) octets\):\s*(.*)$", body[i])
        if m:
            n, hx = int(m.group(1)), m.group(2)
            j = i + 1
            while j < len(body) and len(bytes.fromhex(hx.replace(" ", ""))) < n:
                if re.match(r"^\s+([0-9a-f]{2} ?)+$", body[j]):
                    hx += " " + body[j].strip()
                elif body[j].strip():
                    break
                j += 1
            b = bytes.fromhex(hx.replace(" ", ""))
            assert len(b) == n, (label, n, len(b))
            return b
        i += 1
    raise SystemExit(f"field {label!r} not found")

print("# RFC 8448 section 3 (Simple 1-RTT Handshake), extracted by gen_rfc8448.py from")
print(f"# https://www.rfc-editor.org/rfc/rfc8448.txt sha256 {hashlib.sha256(raw).hexdigest()}")
for w in want:
    head, label, name = w[:3]
    occ = w[3] if len(w) > 3 else 0
    hs = [i for i, l in enumerate(body) if l.startswith("   " + head)]
    b = field(hs[occ], label)
    if name == "sflight" and len(b) != 657:
        raise SystemExit("sflight: wrong record")
    print(name, b.hex())
