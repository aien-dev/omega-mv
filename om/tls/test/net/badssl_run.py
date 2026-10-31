#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only (driver and oracle, no TLS logic of its own): the badssl.com sweep for the
# om/tls client. Called by badssl.sh.
# usage: badssl_run.py CLIENT PKIDIR DASHBOARD.html CACHEDIR OUT.tsv
# For every host on the badssl.com dashboard:
#   live    the client against HOST:PORT itself (system trust store). badssl.com servers offer no
#           TLS 1.3 (probed per host with openssl s_client -tls1_3), so live runs end in the
#           server's alert and never reach the client's certificate checks; they are recorded,
#           not scored, unless a server does speak TLS 1.3.
#   replay  the host's real certificate chain (fetched once with openssl s_client -showcerts and
#           cached in CACHEDIR) presented by hostile.py on 127.0.0.1, the client connecting as
#           HOST with the system trust store. hostile.py signs CertificateVerify with its own key,
#           so a chain the client accepts ends in CERTIFICATE_VERIFY/SIGNATURE_SCHEME (the chain
#           and name checks run first, at the Certificate message), and a refused chain ends in
#           TLS_ERR_CERTIFICATE with the x509 detail.
#   oracle  openssl verify of the same chain against the same trust store, with -verify_hostname
#           and -purpose sslserver: the expected chain verdict.
# Scoring (replay): REFUSE expected and refused = refuse_ok; ACCEPT expected and accepted =
# accept_ok; a mismatch listed in GAPS below = explained; any other mismatch = unexplained.
# Negotiation hosts (cipher suite, key exchange, protocol) are scored on the live result plus
# the ClientHello policy check of hostile.sh (TLS 1.3 only, AEAD suites, x25519).
import html.parser, os, re, socket, subprocess, sys, tempfile, time

client, pki, dash, cache, outp = sys.argv[1:6]
here = os.path.dirname(os.path.abspath(__file__))
STORE = "/etc/ssl/certs/ca-certificates.crt"
# x509 detail codes, from the X5E* constants of om/tls/x509.om:91-111
X5 = ["OK", "MALFORMED", "EXPIRED", "NOT_YET_VALID", "NAME_MISMATCH", "UNKNOWN_ISSUER", "BAD_SIGNATURE",
      "NOT_CA", "PATH_LENGTH", "KEY_USAGE", "EKU", "UNSUPPORTED_ALG", "WEAK_KEY", "CRITICAL_EXT", "TOO_DEEP",
      "LEAF_IS_CA", "NO_SAN", "IP_HOST", "ALG_MISMATCH", "SCHEME", "X5ENC", "X5EEXT"]
# Known, documented gaps: badssl marks these bad for a property this client does not check.
GAPS = {"revoked": "no revocation checking (no OCSP, no CRL)",
        "pinning-test": "no HTTP public key pinning (RFC 7469, retired by browsers)",
        "no-sct": "no Certificate Transparency enforcement",
        "invalid-expected-sct": "no Certificate Transparency enforcement"}
# Client policy that refuses chains a browser accepts, with the reason.
POLICY = {("10000-sans", "MESSAGE_TOO_BIG"): "Certificate message larger than the client's handshake buffer",
          ("incomplete-chain", "CERTIFICATE UNKNOWN_ISSUER"): "no AIA fetching; browsers fetch the missing intermediate"}
NEGOTIATION = {"cipher-suite", "key-exchange", "protocol"}
PAGE = {"mixed-content": "mixed content is a page property", "ui": "a browser UI property",
        "upgrade": "HSTS/upgrade is an HTTP property", "client-certificate": "client certificate handling"}


class Dash(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(); self.sec = None; self.rows = []

    def handle_starttag(self, tag, a):
        a = dict(a)
        if tag == "h2":
            self.sec = a.get("id")
        if tag == "a" and a.get("class") in ("good", "bad", "dubious"):
            m = re.match(r"(https?)://([a-z0-9.-]+\.badssl\.com)(?::(\d+))?/", a.get("href", ""))
            if m:
                self.rows.append((m.group(2), int(m.group(3) or (443 if m.group(1) == "https" else 80)),
                                  self.sec, a["class"], m.group(1)))


def run(cmd, timeout, inp=b""):
    try:
        p = subprocess.run(cmd, input=inp, capture_output=True, timeout=timeout)
        return p.returncode, p.stdout.decode("latin-1"), p.stderr.decode("latin-1")
    except subprocess.TimeoutExpired:
        return None, "", ""


def outcome(rc, out, err):
    m = re.search(r"TLSERR TLS_ERR_([A-Z_]+) detail (\d+)", err)
    if rc is None:
        return "HANG"
    if rc == 0:
        return "ACCEPT"
    if not m:
        return "EXIT%d" % rc
    n, dt = m.group(1), int(m.group(2))
    if n == "CERTIFICATE":
        return "CERTIFICATE " + (X5[dt] if dt < len(X5) else str(dt))
    if n == "ALERT_RECEIVED":
        return "ALERT_RECEIVED %d" % dt
    return n


def fetch_chain(host, port):
    path = os.path.join(cache, "%s_%d.pem" % (host, port))
    if os.path.exists(path) and os.path.getsize(path) > 0:
        return path, "cached"
    for extra in ([], ["-cipher", "ALL:@SECLEVEL=0"], ["-tls1", "-cipher", "ALL:@SECLEVEL=0"],
                  ["-tls1_1", "-cipher", "ALL:@SECLEVEL=0"]):
        rc, out, _ = run(["openssl", "s_client", "-showcerts", "-servername", host, "-connect", "%s:%d" % (host, port)] + extra, 20)
        pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n", out or "", re.S)
        if pems:
            open(path, "w").write("".join(pems))
            open(os.path.join(cache, "fetched.log"), "a").write("%s %s:%d %s %d certs\n" % (
                time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), host, port, " ".join(extra) or "default", len(pems)))
            return path, "fetched"
    # OpenSSL caps a certificate list at 100 KiB ("excessive message size", 10000-sans); tlslite-ng
    # (venv in $TLSLITE_PY, test tooling only) has no such cap
    py = os.environ.get("TLSLITE_PY", os.path.join(here, "../../../../../tlsverify-TlsSuites-ext/venv/bin/python"))
    if os.path.exists(py):
        code = ("import socket,sys,base64\nfrom tlslite.api import TLSConnection\n"
                "c=TLSConnection(socket.create_connection((sys.argv[1],int(sys.argv[2])),timeout=30))\n"
                "c.handshakeClientCert(serverName=sys.argv[1])\n"
                "sys.stdout.write(''.join('-----BEGIN CERTIFICATE-----\\n'+base64.encodebytes(bytes(x.bytes)).decode()"
                "+'-----END CERTIFICATE-----\\n' for x in c.session.serverCertChain.x509List))\n")
        rc, out, _ = run([py, "-c", code, host, str(port)], 60)
        if rc == 0 and "BEGIN CERTIFICATE" in (out or ""):
            open(path, "w").write(out)
            open(os.path.join(cache, "fetched.log"), "a").write("%s %s:%d tlslite-ng %d certs\n" % (
                time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), host, port, out.count("BEGIN CERTIFICATE")))
            return path, "fetched (tlslite-ng)"
    return None, "no chain"


def oracle(host, chain):
    pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n", open(chain).read(), re.S)
    with tempfile.TemporaryDirectory() as t:
        open(t + "/leaf.pem", "w").write(pems[0]); open(t + "/rest.pem", "w").write("".join(pems[1:]) or pems[0])
        rc, out, err = run(["openssl", "verify", "-CAfile", STORE, "-untrusted", t + "/rest.pem",
                            "-verify_hostname", host, "-purpose", "sslserver", t + "/leaf.pem"], 20)
    if rc == 0:
        return "ACCEPT", "openssl verify OK"
    m = re.search(r"error \d+ at \d+ depth lookup: ([^\n]+)", out + err)
    return "REFUSE", "openssl: " + (m.group(1) if m else (out + err).strip().splitlines()[-1][:80])


def freeport():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close()
    return p


def replay(host, chain, port=443):
    lp = freeport()
    srv = subprocess.Popen([sys.executable, os.path.join(here, "hostile.py"), str(lp), pki, "replay", chain],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "READY":
        srv.kill(); return "SERVER-FAIL"
    rc, out, err = run([client, host, str(lp), "get", "127.0.0.1", "-", "/", "0,15000,0"], 40)
    try:
        srv.communicate(timeout=25)
    except subprocess.TimeoutExpired:
        srv.kill(); srv.communicate()
    o = outcome(rc, out, err)
    # chain and name accepted: the handshake got as far as our (deliberately foreign) signature
    return "ACCEPT(chain; then %s)" % o if o in ("CERTIFICATE_VERIFY", "SIGNATURE_SCHEME") else o


def tls13(host, port):
    rc, out, _ = run(["openssl", "s_client", "-tls1_3", "-servername", host, "-connect", "%s:%d" % (host, port)], 20)
    return "yes" if "TLSv1.3" in out and "Cipher is (NONE)" not in out else "no"


d = Dash(); d.feed(open(dash, encoding="utf-8").read())
rows = d.rows
os.makedirs(cache, exist_ok=True)
res = []
cnt = dict(refuse_ok=0, accept_ok=0, explained=0, unexplained=0, na=0)
for host, port, sec, cls, scheme in rows:
    short = host[:-len(".badssl.com")]
    cat = "%s/%s" % (sec, cls)
    if scheme == "http":
        res.append((host, port, cat, "NA (plain HTTP site; no TLS)", "-", "NA", "-", "-")); cnt["na"] += 1; continue
    try:
        socket.getaddrinfo(host, port)
    except OSError:
        res.append((host, port, cat, "NA (host does not resolve)", "DNS", "NA", "-", "-")); cnt["na"] += 1; continue
    live = outcome(*run([client, host, str(port), "get", host, "-", "/", "0,15000,0"], 40))
    t13 = tls13(host, port)
    chain, how = fetch_chain(host, port)
    if sec in NEGOTIATION:
        notes = "tls13=%s; chain %s" % (t13, (replay(host, chain) if chain else how))
        if cls == "good":
            # a server without TLS 1.3 says nothing about this TLS 1.3-only client; scored only if it speaks 1.3
            if t13 == "yes":
                exp = "ACCEPT (server speaks TLS 1.3)"; v = "accept_ok" if live == "ACCEPT" else "unexplained"
            else:
                exp = "NA (server has no TLS 1.3; the client offers nothing else)"; v = "na"
        else:
            exp = "REFUSE (client offers only TLS 1.3, suites 1303/1301, x25519: see the ch_policy line)"
            v = "refuse_ok" if live != "ACCEPT" else "unexplained"
        cnt[v] += 1
        res.append((host, port, cat, exp, "live " + live, v if v != "na" else "NA", live, notes))
        print("%-12s %s:%d %s -> live %s" % (v, host, port, exp[:70], live), flush=True)
        continue
    if not chain:
        res.append((host, port, cat, "NA (no chain fetched)", "-", "NA", live, "tls13=%s" % t13)); cnt["na"] += 1; continue
    oexp, why = oracle(host, chain)
    rep = replay(host, chain)
    acc = rep.startswith("ACCEPT")
    if short in GAPS:
        exp = "REFUSE (badssl %s; %s)" % (cls, why)
    else:
        exp = "%s (%s)" % (oexp, why)
    want_refuse = exp.startswith("REFUSE")
    if want_refuse and not acc:
        v = "refuse_ok"
    elif not want_refuse and acc:
        v = "accept_ok"
    elif want_refuse and acc and short in GAPS:
        v = "explained"; exp += " | gap: " + GAPS[short]
    elif not want_refuse and (short, rep) in POLICY:
        v = "explained"; exp += " | client policy: " + POLICY[(short, rep)]
    else:
        v = "unexplained"
    notes = "tls13=%s; chain %s" % (t13, how)
    if sec in PAGE:
        notes += "; page-level property N/A to a TLS client (%s)" % PAGE[sec]
    if v == "refuse_ok" and rep == "CERTIFICATE MALFORMED":
        notes += ("; client refuses earlier than openssl, at its strict profile checks (x509.om chain_xchk/chain_strict:"
                  " e.g. a leaf without authorityKeyIdentifier, RFC 5280 4.2.1.1)")
    cnt[v] += 1
    res.append((host, port, cat, exp, "replay " + rep, v, live, notes))
    print("%-12s %s:%d %s -> %s" % (v, host, port, exp[:70], rep), flush=True)

# controls: the same machinery must refuse a good chain under the wrong name and with a broken
# leaf signature
ctl_ok = True
good = os.path.join(cache, "sha256.badssl.com_443.pem")
if os.path.exists(good):
    r1 = replay("wrong-name-control.badssl.org", good)
    pems = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----\n", open(good).read(), re.S)
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
    der = bytearray(x509.load_pem_x509_certificate(pems[0].encode()).public_bytes(serialization.Encoding.DER))
    der[-8] ^= 0x01                                  # inside the leaf's signature value
    import base64
    bad = os.path.join(cache, "control-badsig.pem")
    b64 = base64.encodebytes(bytes(der)).decode()
    open(bad, "w").write("-----BEGIN CERTIFICATE-----\n" + b64 + "-----END CERTIFICATE-----\n" + "".join(pems[1:]))
    r2 = replay("sha256.badssl.com", bad)
    r3 = replay("sha256.badssl.com", good)
    for name, got, want in (("wrong name", r1, "CERTIFICATE NAME_MISMATCH"), ("bad leaf signature", r2, "CERTIFICATE BAD_SIGNATURE"),
                            ("good chain", r3, "ACCEPT")):
        ok = got.startswith(want)
        ctl_ok &= ok
        print("CONTROL %s %s: %s (want %s)" % ("PASS" if ok else "FAIL", name, got, want))
else:
    ctl_ok = False
    print("CONTROL FAIL: no sha256.badssl.com chain")

with open(outp, "w") as f:
    f.write("# badssl.com sweep of the om/tls client; generated by om/tls/test/net/badssl.sh on %s\n" % time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
    f.write("# observed: 'replay X' = the host's real chain served locally (see badssl_run.py); 'live X' = the real server\n")
    f.write("host\tport\tcategory\texpected\tobserved\tverdict\tlive\tnotes\n")
    for r in res:
        f.write("\t".join(str(x) for x in r) + "\n")
ok = cnt["unexplained"] == 0 and ctl_ok
print("BADSSL %s refuse_ok=%d accept_ok=%d explained=%d unexplained=%d na=%d hosts=%d" % (
    "PASS" if ok else "FAIL", cnt["refuse_ok"], cnt["accept_ok"], cnt["explained"], cnt["unexplained"], cnt["na"], len(rows)))
sys.exit(0 if ok else 1)
