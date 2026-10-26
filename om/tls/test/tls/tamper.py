#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only: a one-connection TCP proxy 127.0.0.1:LISTEN -> 127.0.0.1:BACKEND that
# forwards TLS records unchanged, except that it flips one bit inside the body of the first
# server-to-client application_data record that follows the server's handshake flight, i.e. the
# first type-23 record sent after the client's Finished has been forwarded.
import select, socket, sys

lp, bp = int(sys.argv[1]), int(sys.argv[2])
ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
ls.bind(("127.0.0.1", lp)); ls.listen(1)
c, _ = ls.accept()
s = socket.create_connection(("127.0.0.1", bp))
cbuf, sbuf = b"", b""
client_records = 0
done = False
while True:
    r, _, _ = select.select([c, s], [], [], 20)
    if not r:
        break
    if c in r:
        b = c.recv(65536)
        if not b:
            break
        cbuf += b
        while len(cbuf) >= 5 and len(cbuf) >= 5 + int.from_bytes(cbuf[3:5], "big"):
            n = 5 + int.from_bytes(cbuf[3:5], "big")
            client_records += 1
            s.sendall(cbuf[:n]); cbuf = cbuf[n:]
    if s in r:
        b = s.recv(65536)
        if not b:
            break
        sbuf += b
        while len(sbuf) >= 5 and len(sbuf) >= 5 + int.from_bytes(sbuf[3:5], "big"):
            n = 5 + int.from_bytes(sbuf[3:5], "big")
            rec = bytearray(sbuf[:n]); sbuf = sbuf[n:]
            # client records: ClientHello, change_cipher_spec, Finished, then the request
            if not done and rec[0] == 23 and client_records >= 4:
                rec[5 + (n - 5) // 2] ^= 1
                done = True
                print("flipped one bit in a server record of", n, "bytes")
            c.sendall(bytes(rec))
c.close(); s.close()
