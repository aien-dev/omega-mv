#!/usr/bin/env python3
# Secret residue check: do handshake secrets survive in the client's memory after the
# handshake, and do any keys survive tls_close?  usage: residue.py CLIENT PKIDIR [--keep] [--approved]
# --approved runs the client in approved-only mode (OPTS APPROVED=1) against fakesrv.py ap-hold:S
# (secp256r1 ECDH, TLS_AES_128_GCM_SHA256): "shared" is then the P-256 Z kept at c[TLECZ].
#
# fakesrv.py (mode hold:S) completes a handshake, writes every secret it derived to a file,
# waits S seconds and then answers. During the wait the client is blocked in tls_read with the
# handshake over; after the reply the client calls tls_close and (OPTS PAUSEMS) sleeps. At both
# points this script reads the client's writable private mappings through /proc/PID/mem (the
# client is its child, so Yama ptrace_scope 1 allows it) and counts every occurrence of each
# secret. Secrets are 32 bytes (16 for an AES key), so a chance match is impossible.
#
# Expectations (fail closed):
#   after the handshake: c_ap/s_ap secrets, keys and ivs present (positive control: the scan
#     works; they live in c[TLCAP..], c[TLWKEY..]); shared, early, hs, c_hs, s_hs, ms, the
#     handshake keys and ivs and the finished keys absent everywhere.
#   after tls_close: every secret absent.
# Prints RESIDUE PASS or RESIDUE FAIL with the offending secret names and the mapping they
# were found in. Exit 0 / 1; 3 (NOT_RUN) when /proc/PID/mem cannot be read.
import os, socket, subprocess, sys, tempfile, time

HS_ONLY = ("shared", "early", "hs", "c_hs", "s_hs", "ms", "c_hs_key", "s_hs_key",
           "c_hs_iv", "s_hs_iv", "c_fin", "s_fin")
AP = ("c_ap", "s_ap", "c_ap_key", "s_ap_key", "c_ap_iv", "s_ap_iv")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def scan(pid, names, values):
    """{name: [(mapping, count), ...]} over every writable private mapping of pid; values[i] is the
    secret called names[i]. Only names, mapping names and counts leave this function."""
    found = {n: [] for n in names}
    with open("/proc/%d/maps" % pid) as mf, open("/proc/%d/mem" % pid, "rb", 0) as mem:
        for line in mf:
            f = line.split()
            lo, hi = (int(x, 16) for x in f[0].split("-"))
            perms, name = f[1], (f[5] if len(f) > 5 else "anon")
            if "w" not in perms or "p" not in perms or name.startswith("[v"):
                continue
            try:
                mem.seek(lo)
                data = mem.read(hi - lo)
            except OSError:
                continue
            for i, v in enumerate(values):
                k = data.count(v)
                if k:
                    found[names[i]].append((name, k))
    return found


def main():
    client, pki = sys.argv[1], sys.argv[2]
    keep = "--keep" in sys.argv
    approved = "--approved" in sys.argv
    work = tempfile.mkdtemp(prefix="residue-")
    port = free_port()
    keyfile = os.path.join(work, "keys.txt")
    here = os.path.dirname(os.path.abspath(__file__))
    env = dict(os.environ, FAKESRV_KEYS=keyfile)
    srv = subprocess.Popen([sys.executable, os.path.join(here, "fakesrv.py"), str(port), pki,
                            "ap-hold:4" if approved else "hold:4"],
                           stdout=open(os.path.join(work, "srv.log"), "w"), stderr=subprocess.STDOUT, env=env)
    time.sleep(0.6)
    cl = subprocess.Popen([client, "localhost", str(port), "get", "127.0.0.1",
                           os.path.join(pki, "root.pem"), "/", "0,0,0,4000,1" if approved else "0,0,0,4000"],
                          stdout=open(os.path.join(work, "client.out"), "w"),
                          stderr=open(os.path.join(work, "client.err"), "w"))
    t0 = time.time()
    while not os.path.exists(keyfile) and time.time() - t0 < 20:
        time.sleep(0.05)
    if not os.path.exists(keyfile):
        print("RESIDUE NOT_RUN: fakesrv wrote no keys (%s)" % work)
        cl.kill(); srv.kill()
        return 3
    time.sleep(1.0)  # the client has consumed Finished and is blocked in tls_read
    names, values = [], []
    for line in open(keyfile):
        n, h = line.split()
        names.append(n)
        values.append(bytes.fromhex(h))
    try:
        after_hs = scan(cl.pid, names, values)
    except PermissionError as e:
        print("RESIDUE NOT_RUN: /proc/%d/mem unreadable (%s)" % (cl.pid, e))
        cl.kill(); srv.kill()
        return 3
    # wait for the reply, tls_close and the client's pause
    t0 = time.time()
    while time.time() - t0 < 20:
        out = open(os.path.join(work, "client.out"), "rb").read()
        if b"HTTP/1.0 200 ok" in out and cl.poll() is None:
            break
        time.sleep(0.05)
    time.sleep(0.5)
    if cl.poll() is not None:
        print("RESIDUE NOT_RUN: client exited before the post-close scan (exit %s)" % cl.returncode)
        srv.kill()
        return 3
    after_close = scan(cl.pid, names, values)
    cl.wait(timeout=20)
    srv.wait(timeout=20)
    bad = []
    for n in HS_ONLY:
        if after_hs[n]:
            bad.append("after handshake: %s in %s" % (n, after_hs[n]))
    for n in AP:
        if not after_hs[n]:
            bad.append("after handshake: positive control %s not found (scan broken)" % n)
    for n in names:
        if after_close[n]:
            bad.append("after tls_close: %s in %s" % (n, after_close[n]))
    for n in sorted(names):
        print("residue %-9s after-handshake=%-24s after-close=%s" % (n, after_hs[n] or "-", after_close[n] or "-"))
    print("client exit %d, %s" % (cl.returncode, open(os.path.join(work, "client.err")).read().strip() or "no stderr"))
    if cl.returncode != 0:
        bad.append("client exit %d" % cl.returncode)
    if bad:
        print("RESIDUE FAIL\n  " + "\n  ".join(bad))
        return 1
    print("RESIDUE PASS secrets=%d hs_only=%d" % (len(names), len(HS_ONLY)))
    if not keep:
        subprocess.call(["rm", "-rf", work])
    return 0


if __name__ == "__main__":
    sys.exit(main())
