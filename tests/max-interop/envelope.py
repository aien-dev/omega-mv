# SPDX-License-Identifier: AGPL-3.0-or-later
"""Cross-runtime evidence envelope v1 and the C2 receipt binding.

The envelope is canonical text. envelope_id = SHA-256 over the lines from `op=`
through `timings=` inclusive, each newline-terminated. Receipt fields follow and
are not part of the id: the receipt binds the id, so including the receipt in
the id would be circular. Structural validation lives here; signature
verification stays in the existing C2 receipt code (receipt_tool).

Proof tiers are kept separate: hashes are identity, a signature is provenance,
the bounded oracle is a result within its scope, and `measured` is execution.
"""
import hashlib
import json
import os
import subprocess

FIELDS = [
    "op", "contract_id", "artifact_id", "artifact_cubin_sha256",
    "artifact_qmd0_sha256", "artifact_qmd1_sha256", "semantic_spec_id",
    "isa", "machine_profile", "runtime", "runtime_version",
    "runtime_source_commit", "device_backend", "device_identity",
    "input_sha256", "output_sha256", "oracle", "oracle_verdict",
    "tolerance", "execution", "timings", "receipt_sha256", "receipt_id",
    "proof_tiers",
]
HEADER = "omega-cross-runtime-envelope/1"
MEASURED_TIER = "identity:hash,provenance:signature,result:bounded-check,execution:measured"
NOT_RUN_TIER = "identity:hash,provenance:signature,result:bounded-check,execution:not-run"
HEX64 = set("0123456789abcdef")


class Refused(Exception):
    def __init__(self, code):
        super().__init__(code)
        self.code = code


def _hex64(s):
    return isinstance(s, str) and len(s) == 64 and set(s) <= HEX64


def _token(s, lo=1, hi=64):
    return isinstance(s, str) and lo <= len(s) <= hi and all(
        32 < ord(c) < 127 and c not in " \t\n" for c in s)


def manifest_fields(prefix):
    d = {}
    with open(prefix + ".manifest") as f:
        for line in f:
            if "=" in line:
                k, v = line.rstrip("\n").split("=", 1)
                d[k] = v
    return d


def build(artifact_prefix, fixture, output_bin=None, runtime="omega-native",
          runtime_version="cudapp-artifact-export/1", source_commit="UNKNOWN",
          device_backend="omega-native", device_identity=None,
          execution="NOT_RUN", verdict="PASS", tolerance=None):
    a = manifest_fields(artifact_prefix)
    if device_identity is None:
        device_identity = hashlib.sha256(b"GB10 sm_121 qualification NOT_RUN").hexdigest()
    if tolerance is None:
        tolerance = a["tolerance"]
    inputs = {k: fixture["inputs"][k] for k in fixture["inputs"]}
    input_sha = hashlib.sha256(json.dumps(inputs, separators=(",", ":")).encode()).hexdigest()
    have_output = bool(output_bin) and os.path.exists(output_bin)
    if have_output:
        output_sha = hashlib.sha256(open(output_bin, "rb").read()).hexdigest()
    else:
        output_sha = "0" * 64
        if verdict == "PASS":
            verdict = "NOT_RUN"
    f = {
        "op": a["op"], "contract_id": a["contract_id"], "artifact_id": a["artifact_id"],
        "artifact_cubin_sha256": a["cubin_sha256"],
        "artifact_qmd0_sha256": a["qmd0_sha256"],
        "artifact_qmd1_sha256": a["qmd1_sha256"],
        "semantic_spec_id": a["semantic_spec_id"], "isa": a["isa"],
        "machine_profile": a["machine_profile"], "runtime": runtime,
        "runtime_version": runtime_version, "runtime_source_commit": source_commit,
        "device_backend": device_backend, "device_identity": device_identity,
        "input_sha256": input_sha, "output_sha256": output_sha,
        "oracle": "independent-fp64-v1", "oracle_verdict": verdict,
        "tolerance": tolerance, "execution": execution,
        "timings": "compile=host;run=host" if execution == "NOT_RUN" else "compile=measured;run=measured",
        "receipt_sha256": "0" * 64, "receipt_id": "0" * 64,
        "proof_tiers": NOT_RUN_TIER if execution == "NOT_RUN" else MEASURED_TIER,
    }
    return f


def identity_bytes(f):
    return "".join(f"{k}={f[k]}\n" for k in FIELDS[:21]).encode()


def envelope_id(f):
    return hashlib.sha256(identity_bytes(f)).hexdigest()


def render(f):
    eid = envelope_id(f)
    lines = [HEADER, "envelope_id=" + eid] + [f"{k}={f[k]}" for k in FIELDS]
    return "\n".join(lines) + "\n", eid


def parse(text):
    lines = text.splitlines()
    if len(lines) != 26:
        raise Refused("E_ORDER")
    if lines[0] != HEADER:
        raise Refused("E_HEADER")
    if not lines[1].startswith("envelope_id="):
        raise Refused("E_ORDER")
    f = {}
    for want, line in zip(FIELDS, lines[2:]):
        if not line.startswith(want + "="):
            raise Refused("E_ORDER")
        f[want] = line[len(want) + 1:]
    if envelope_id(f) != lines[1][12:]:
        raise Refused("E_ID")
    return f


def validate(text):
    f = parse(text)
    for k in ("contract_id", "artifact_id", "artifact_cubin_sha256",
              "artifact_qmd0_sha256", "artifact_qmd1_sha256", "semantic_spec_id",
              "machine_profile", "device_identity", "input_sha256",
              "output_sha256", "receipt_sha256", "receipt_id"):
        if not _hex64(f[k]):
            raise Refused("E_VALUE")
    if f["op"] not in ("matmul", "gqa-causal-attention"):
        raise Refused("E_OP")
    if f["isa"] != "sm_121":
        raise Refused("E_ISA")
    if f["runtime"] not in ("omega-native", "modular-max"):
        raise Refused("E_RUNTIME")
    if f["runtime_source_commit"] != "UNKNOWN" and not (
            len(f["runtime_source_commit"]) == 40 and set(f["runtime_source_commit"]) <= HEX64):
        raise Refused("E_RUNTIME")
    if f["device_backend"] not in ("cuda", "omega-native"):
        raise Refused("E_BACKEND")
    if f["oracle"] != "independent-fp64-v1":
        raise Refused("E_ORACLE")
    if f["oracle_verdict"] not in ("PASS", "FAIL", "NOT_RUN"):
        raise Refused("E_VERDICT")
    if f["execution"] not in ("measured", "NOT_RUN"):
        raise Refused("E_EXECUTION")
    if f["execution"] == "measured" and f["oracle_verdict"] != "PASS":
        raise Refused("E_EXECUTION")
    if not _token(f["runtime_version"]) or not _token(f["tolerance"]) or not _token(f["timings"]):
        raise Refused("E_VALUE")
    want = MEASURED_TIER if f["execution"] == "measured" else NOT_RUN_TIER
    if f["proof_tiers"] != want:
        raise Refused("E_TIERS")
    return f, envelope_id(f)


def issue(receipt_tool, f, out_path):
    """Issue a TEST-class C2 receipt binding envelope_id, cubin, input, artifact."""
    eid = envelope_id(f)
    cmd = [receipt_tool, "issue", eid, f["artifact_cubin_sha256"],
           f["input_sha256"], f["artifact_id"], out_path]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(p.stdout + p.stderr)
    return p.stdout.strip()


def verify(receipt_tool, receipt_path, f, epoch_floor="1"):
    eid = envelope_id(f)
    cmd = [receipt_tool, "verify", receipt_path, eid,
           f["artifact_cubin_sha256"], f["input_sha256"], f["artifact_id"], str(epoch_floor)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr).strip()
