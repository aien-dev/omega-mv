#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for the cross-runtime envelope and its C2 receipt binding.

Requires build/receipt-tool (tests/max-interop/build_receipt.sh) and a CUDApp
artifact whose manifest is next to it. Exit 0 only when every positive and
negative case behaves as named.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import envelope  # noqa: E402


class T:
    def __init__(self):
        self.n = 0
        self.fail = 0

    def ok(self, cond, msg):
        self.n += 1
        if not cond:
            self.fail += 1
            print("FAIL:", msg)

    def refused(self, fn, code, msg):
        self.n += 1
        try:
            fn()
        except envelope.Refused as e:
            if e.code != code:
                self.fail += 1
                print(f"FAIL: {msg}: expected {code}, got {e.code}")
        except Exception as e:  # noqa: BLE001
            self.fail += 1
            print(f"FAIL: {msg}: unexpected {type(e).__name__}: {e}")
        else:
            self.fail += 1
            print(f"FAIL: {msg}: accepted")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--artifact", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--receipt-tool", default=os.path.join(HERE, "../../build/receipt-tool"))
    ap.add_argument("--omega-output", default=None)
    a = ap.parse_args()
    t = T()
    tmp = tempfile.mkdtemp(prefix="omega-envelope-")
    fx = json.load(open(a.fixture))

    f = envelope.build(a.artifact, fx, a.omega_output)
    text, eid = envelope.render(f)
    t.ok(envelope.validate(text)[1] == eid, "fresh envelope validates with its id")
    t.ok(f["execution"] == "NOT_RUN", "host envelope is NOT_RUN")

    # Sign, embed, validate, verify.
    rpath = os.path.join(tmp, "e.receipt")
    out = envelope.issue(a.receipt_tool, f, rpath)
    rb = open(rpath, "rb").read()
    t.ok(len(rb) == 416, "receipt is 416 bytes")
    f["receipt_sha256"] = hashlib.sha256(rb).hexdigest()
    f["receipt_id"] = out.split("receipt_id=")[1]
    text, eid2 = envelope.render(f)
    t.ok(eid2 == eid, "receipt fields do not change envelope_id")
    t.ok(envelope.validate(text)[1] == eid, "signed envelope validates")
    rc, msg = envelope.verify(a.receipt_tool, rpath, f)
    t.ok(rc == 0, f"receipt verifies: {msg}")

    # A changed bound value invalidates the receipt (program/executable binding).
    g = dict(f)
    g["artifact_cubin_sha256"] = "9" * 64
    rg = os.path.join(tmp, "g.receipt")
    t.ok(envelope.verify(a.receipt_tool, rpath, g)[0] != 0, "receipt rejects changed cubin digest")
    g2 = dict(f)
    g2["output_sha256"] = "8" * 64
    t.ok(envelope.verify(a.receipt_tool, rpath, g2)[0] != 0, "receipt rejects changed envelope id")

    # Structural negatives (re-render so the id is coherent where intended).
    def render_with(**kw):
        h = dict(f)
        h.update(kw)
        return envelope.render(h)[0]

    envelope.validate(render_with())  # sanity
    stale = text.replace("artifact_qmd0_sha256=" + f["artifact_qmd0_sha256"],
                         "artifact_qmd0_sha256=" + "7" * 64)
    t.refused(lambda: envelope.parse(stale), "E_ID", "stale id after QMD change")
    t.refused(lambda: envelope.validate(render_with(isa="sm_120")), "E_ISA", "wrong ISA")
    t.refused(lambda: envelope.validate(render_with(runtime="cuda-runtime")), "E_RUNTIME", "wrong runtime")
    t.refused(lambda: envelope.validate(render_with(device_backend="rocm")), "E_BACKEND", "wrong backend")
    t.refused(lambda: envelope.validate(render_with(oracle="trust-me")), "E_ORACLE", "unknown oracle")
    t.refused(lambda: envelope.validate(render_with(oracle_verdict="MAYBE")), "E_VERDICT", "unknown verdict")
    t.refused(lambda: envelope.validate(render_with(execution="measured")), "E_EXECUTION",
              "measured execution with non-PASS verdict")
    t.refused(lambda: envelope.validate(render_with(execution="measured", oracle_verdict="PASS")),
              "E_TIERS", "measured execution without the measured tier")
    t.refused(lambda: envelope.validate(render_with(receipt_sha256="0" * 63)), "E_VALUE", "short receipt digest")
    t.refused(lambda: envelope.validate(render_with(tolerance="")), "E_VALUE", "empty tolerance")
    t.refused(lambda: envelope.validate("x\n"), "E_ORDER", "truncated envelope")
    t.refused(lambda: envelope.validate(render_with(contract_id="0" * 64)),
              "E_CONTRACT", "op/contract mismatch")
    h = dict(f); h["receipt_sha256"] = "0" * 64
    t.ok(envelope.verify(a.receipt_tool, rpath, h)[0] != 0, "changed receipt_sha256 rejected")
    h = dict(f); h["receipt_id"] = "f" * 64
    t.ok(envelope.verify(a.receipt_tool, rpath, h)[0] != 0, "changed receipt_id rejected")
    t.refused(lambda: envelope.validate(render_with(
        execution="measured", oracle_verdict="PASS",
        proof_tiers=envelope.MEASURED_TIER, timings="compile=measured;run=measured",
        output_sha256="0" * 64)), "E_RESULT", "measured PASS with no output")
    # Duplicate/extra line.
    lines = render_with().splitlines()
    t.refused(lambda: envelope.validate("\n".join(lines + ["extra=1"]) + "\n"), "E_ORDER", "extra line")

    # Stale receipt and a bad signature.
    t.ok(envelope.verify(a.receipt_tool, rpath, f, epoch_floor="2")[0] != 0, "stale receipt rejected")
    bad = bytearray(rb)
    bad[400] ^= 0x01
    bpath = os.path.join(tmp, "bad.receipt")
    open(bpath, "wb").write(bad)
    t.ok(envelope.verify(a.receipt_tool, bpath, f)[0] != 0, "tampered signature rejected")

    print(json.dumps({"checks": t.n, "fail": t.fail, "envelope_id": eid,
                      "receipt_id": f["receipt_id"]}, sort_keys=True))
    shutil.rmtree(tmp, ignore_errors=True)
    return 1 if t.fail else 0


if __name__ == "__main__":
    sys.exit(main())
