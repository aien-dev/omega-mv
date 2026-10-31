#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Test tooling only: runs every hostile.tsv case (or the ones named after the table) against a
# build of test/tls/client.om and judges it. One hostile.py server per case on 127.0.0.1.
# usage: hostile_run.py CLIENT PKIDIR TSV [CASE...]
# Prints one row per case and "HOSTILE PASS|FAIL cases=N pass=P fail=F"; exit 0 only on PASS.
# Client outcome: OK = exit 0 and "HTTP/1.0 200 ok" on stdout; otherwise the TLSERR name from
# stderr; HANG = killed by the 40 s timeout; TRAP = any other exit without a TLSERR line.
import os, re, socket, subprocess, sys

client, pki, tsv = sys.argv[1], sys.argv[2], sys.argv[3]
only = set(sys.argv[4:])
here = os.path.dirname(os.path.abspath(__file__))
rows = []
for line in open(tsv):
    if line.startswith("#") or not line.strip():
        continue
    f = line.rstrip("\n").split("\t")
    if len(f) != 6:
        print("TABLE FAULT: %r" % line); sys.exit(2)
    if not only or f[0] in only:
        rows.append(f)
if only and len(rows) != len(only):
    print("TABLE FAULT: unknown case in %s" % sorted(only - {r[0] for r in rows})); sys.exit(2)


def freeport():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close()
    return p


npass = nfail = 0
for case, expect, alerts, check, rfc, desc in rows:
    port = freeport()
    srv = subprocess.Popen([sys.executable, os.path.join(here, "hostile.py"), str(port), pki, case],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if srv.stdout.readline().strip() != "READY":
        srv.kill(); print("FAIL | %-22s | server did not start: %s" % (case, srv.stderr.read()[-300:])); nfail += 1; continue
    try:
        cp = subprocess.run([client, "localhost", str(port), "get", "127.0.0.1", os.path.join(pki, "root.pem"), "/", "0,8000,0"],
                            capture_output=True, timeout=40)
        rc, cout, cerr = cp.returncode, cp.stdout.decode("latin-1"), cp.stderr.decode("latin-1")
    except subprocess.TimeoutExpired:
        rc, cout, cerr = None, "", ""
    try:
        sout, serr = srv.communicate(timeout=25)
    except subprocess.TimeoutExpired:
        srv.kill(); sout, serr = srv.communicate()
    facts = {}
    for l in sout.splitlines():
        if l.startswith("SRV "):
            facts.update(kv.split("=", 1) for kv in l[4:].split() if "=" in kv)
    m = re.search(r"TLSERR TLS_ERR_([A-Z_]+) detail (\d+)", cerr)
    if rc is None:
        got = "HANG"
    elif rc == 0 and "HTTP/1.0 200 ok" in cout:
        got = "OK"
    elif m:
        got = m.group(1)
    else:
        got = "TRAP(rc=%s)" % rc
    alert = facts.get("alert", "none")
    why = []
    if "CLOSE_NOT_FAILED" in cerr:
        why.append("tls_close revived a failed connection")
    if expect == "OK":
        if got != "OK":
            why.append("expected OK")
    elif expect == "ABORT_OR_OK":
        if got != "OK" and not m:
            why.append("neither OK nor a TLS error")
    else:
        if not m or got == "OK":
            why.append("expected an abort")
        elif got == "TIMEOUT" and expect != "ABORT:TIMEOUT":
            # the client's own handshake budget ran out: it waited for a server that stopped
            # sending, so the case never reached the condition it tests
            why.append("client timed out waiting (case not exercised)")
        elif ":" in expect and got not in expect.split(":", 1)[1].split(","):
            why.append("expected TLS_ERR_%s" % expect.split(":", 1)[1])
    if m and alerts not in ("*", "-") and got not in ("ALERT_RECEIVED", "EOF", "TIMEOUT", "IO"):
        if alert not in alerts.split(","):
            why.append("alert %s, RFC wants %s" % (alert, alerts))
    if check != "-":
        for ck in check.split(","):
            if ck.startswith("!out:"):
                if ck[5:] in cout:
                    why.append("stdout has %s" % ck[5:])
            else:
                k, v = ck.split("=", 1)
                if facts.get(k) != v:
                    why.append("server saw %s=%s, wants %s" % (k, facts.get(k), v))
    if serr.strip() and "Traceback" in serr:
        why.append("server error: " + serr.strip().splitlines()[-1])
    v = "FAIL" if why else "PASS"
    npass += not why; nfail += bool(why)
    extra = " ".join("%s=%s" % kv for kv in facts.items() if kv[0] != "alert")
    print("%s | %-22s | expect %-28s alert %-9s | got %-22s alert %-5s | %s | %s%s" % (
        v, case, expect, alerts, got, alert, extra or "-", rfc, (" | WHY: " + "; ".join(why)) if why else ""), flush=True)
print("HOSTILE %s cases=%d pass=%d fail=%d" % ("PASS" if nfail == 0 else "FAIL", npass + nfail, npass, nfail))
sys.exit(0 if nfail == 0 else 1)
