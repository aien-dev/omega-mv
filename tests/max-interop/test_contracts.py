#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Acceptance tests for the MAX interop op contracts (omega-mv#680).

The tests have three independent legs:

* the native Omega checker (`build/contract-check`) accepts the canonical
  contract bytes and rejects every mutation with a named code;
* an independent pure-Python validator applies the same rules (so a checker
  regression cannot make the suite vacuous);
* the pure-Python numeric oracle checks the contract semantics and the
  deliberate wrong implementations in the mutation corpus.

Fixtures are regenerated from the deterministic splitmix64 generator and
compared byte-for-byte with the committed JSON.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SPEC = ROOT / "spec" / "max-interop"
FIXDIR = HERE / "fixtures"
CHECKER = ROOT / "build" / "contract-check"

sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE))
import oracle  # noqa: E402

CONTRACTS = {
    "matmul-bf16-v1": oracle.MATMUL_CONTRACT_ID,
    "gqa-causal-v1": oracle.GQA_CONTRACT_ID,
}
SCHEMA: dict[str, list[str]] = {}


def contract_path(name: str) -> Path:
    return SPEC / (name + ".contract")


def contract_bytes(name: str) -> bytes:
    return contract_path(name).read_bytes()


def contract_text(name: str) -> str:
    return contract_bytes(name).decode("ascii")


def load_schemas() -> None:
    for name in CONTRACTS:
        lines = contract_text(name)[:-1].split("\n")
        op = lines[1].split("=", 1)[1]
        SCHEMA[op] = lines


# ---------------------------------------------------------------------------
# independent Python validators (not a proxy for the .om binary)


def _lines(text: str) -> list[str] | None:
    if not text.endswith("\n"):
        return None
    return text[:-1].split("\n")


def _key(line: str) -> str | None:
    if "=" not in line:
        return None
    return line.split("=", 1)[0]


def _value(line: str) -> str | None:
    if "=" not in line:
        return None
    return line.split("=", 1)[1]


HEX64 = re.compile(r"^[0-9a-f]{64}$")
FIXTURE_NAME = re.compile(r"^[A-Za-z0-9._-]{1,64}$")


def validate_contract(text: str) -> str | None:
    """Return the named refusal code, or None when the contract is canonical."""
    lines = _lines(text)
    if lines is None:
        return "E_ORDER"
    if not lines or lines[0] != "omega-op-contract/1":
        return "E_HEADER"
    if len(lines) < 2:
        return "E_MISSING"
    if _key(lines[1]) != "op":
        return "E_ORDER"
    op = _value(lines[1])
    if op not in SCHEMA:
        return "E_VALUE"
    expected = SCHEMA[op]
    exp_keys = [_key(x) for x in expected]
    keys = [_key(x) for x in lines]
    for key in exp_keys:
        if key not in keys:
            return "E_MISSING"
    for i, want in enumerate(expected):
        if i >= len(lines):
            return "E_MISSING"
        got = lines[i]
        if got == want:
            continue
        gk = _key(got)
        if gk is None:
            return "E_ORDER"
        wk = _key(want)
        if gk == wk:
            if wk is not None and wk.startswith("dtype_"):
                return "E_DTYPE"
            if wk is not None and (wk.startswith("layout_") or "stride" in wk):
                return "E_LAYOUT"
            if wk is not None and wk.startswith("tolerance"):
                return "E_TOLERANCE"
            return "E_VALUE"
        if gk in exp_keys:
            return "E_ORDER"
        return "E_UNKNOWN"
    for got in lines[len(expected) :]:
        gk = _key(got)
        if gk is None:
            return "E_ORDER"
        if gk not in exp_keys:
            return "E_UNKNOWN"
        return "E_ORDER"
    return None


def validate_manifest(text: str, expected_id: str) -> str | None:
    lines = _lines(text)
    if lines is None:
        return "E_ORDER"
    if len(lines) < 2:
        return "E_HEADER"
    if _key(lines[0]) != "expect":
        return "E_HEADER"
    if lines[1] != "omega-input-manifest/1":
        return "E_HEADER"
    required = ["op", "contract", "fixture", "input_sha256", "seed", "count"]
    keys = [_key(x) for x in lines]
    for key in required:
        if key not in keys:
            return "E_MISSING"
    if len(lines) < 8:
        return "E_MISSING"
    for i, key in enumerate(required, start=2):
        if keys[i] != key:
            return "E_ORDER" if keys[i] in required else "E_UNKNOWN"
    if len(lines) > 9:
        return "E_UNKNOWN"
    if len(lines) == 9 and keys[8] != "payload_hex":
        return "E_UNKNOWN"

    expect_id = _value(lines[0]) or ""
    op = _value(lines[2]) or ""
    contract = _value(lines[3]) or ""
    fixture = _value(lines[4]) or ""
    input_sha = _value(lines[5]) or ""
    seed = _value(lines[6]) or ""
    count = _value(lines[7]) or ""
    if not HEX64.match(expect_id):
        return "E_VALUE"
    if op not in ("matmul", "gqa-causal-attention"):
        return "E_VALUE"
    if not HEX64.match(contract) or contract not in CONTRACTS.values():
        return "E_VALUE"
    if contract != expect_id:
        return "E_TARGET"
    if (op == "matmul" and contract != oracle.MATMUL_CONTRACT_ID) or (
        op == "gqa-causal-attention" and contract != oracle.GQA_CONTRACT_ID
    ):
        return "E_TARGET"
    if expect_id != expected_id:
        return "E_TARGET"
    if not FIXTURE_NAME.match(fixture):
        return "E_VALUE"
    if not HEX64.match(input_sha):
        return "E_VALUE"
    if not seed.isdigit() or int(seed) > 2**64 - 1:
        return "E_VALUE"
    if not count.isdigit() or int(count) > 2**32 - 1:
        return "E_VALUE"
    if len(lines) == 9:
        payload_hex = _value(lines[8]) or ""
        if len(payload_hex) == 0 or len(payload_hex) % 2 or not re.match(
            r"^[0-9a-f]+$", payload_hex
        ):
            return "E_VALUE"
        if hashlib.sha256(bytes.fromhex(payload_hex)).hexdigest() != input_sha:
            return "E_VALUE"
    return None


# ---------------------------------------------------------------------------
# native checker driver


def run_checker(mode: str, payload: bytes) -> tuple[int, str, str]:
    assert CHECKER.exists(), f"missing {CHECKER}; run tools/max-interop/build.sh"
    proc = subprocess.run(
        [str(CHECKER)],
        input=b"mode=" + mode.encode("ascii") + b"\n" + payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return proc.returncode, proc.stdout.decode("utf-8", "replace"), proc.stderr.decode(
        "utf-8", "replace"
    )


def checker_contract(name: str) -> None:
    rc, out, err = run_checker("check", contract_bytes(name))
    assert rc == 0, (rc, out, err)
    assert out == "OK " + CONTRACTS[name] + "\n", out


def checker_contract_rejects(text: str, code: str) -> None:
    rc, out, err = run_checker("check", text.encode("ascii"))
    assert rc == 1, (rc, out, err)
    assert out == f"REFUSED {code}\n", out


def checker_manifest(payload: str, expected: str, code: str | None) -> None:
    rc, out, err = run_checker("manifest", payload.encode("ascii"))
    if code is None:
        assert rc == 0, (rc, out, err)
        assert out == "OK " + expected + "\n", out
    else:
        assert rc == 1, (rc, out, err)
        assert out == f"REFUSED {code}\n", out


def manifest_text(
    payload: bytes,
    contract: str,
    op: str,
    fixture: str,
    expected: str | None = None,
    seed: int = 680,
    count: int = 4,
    with_payload: bool = True,
) -> str:
    if expected is None:
        expected = contract
    text = (
        f"expect={expected}\n"
        "omega-input-manifest/1\n"
        f"op={op}\n"
        f"contract={contract}\n"
        f"fixture={fixture}\n"
        f"input_sha256={hashlib.sha256(payload).hexdigest()}\n"
        f"seed={seed}\n"
        f"count={count}\n"
    )
    if with_payload:
        text += "payload_hex=" + payload.hex() + "\n"
    return text


# ---------------------------------------------------------------------------
# tests


def test_contract_ids_match_sha256sum() -> None:
    for name, want in CONTRACTS.items():
        raw = contract_bytes(name)
        assert hashlib.sha256(raw).hexdigest() == want, name
        if shutil.which("sha256sum"):
            proc = subprocess.run(
                ["sha256sum", str(contract_path(name))],
                stdout=subprocess.PIPE,
                check=True,
                text=True,
            )
            assert proc.stdout.split()[0] == want, proc.stdout


def test_checker_contract_ids() -> None:
    for name, want in CONTRACTS.items():
        rc, out, err = run_checker("id", contract_bytes(name))
        assert rc == 0, (rc, out, err)
        assert out == want + "\n", out


def test_checker_sha256() -> None:
    payloads = [
        b"",
        b"abc",
        contract_bytes("matmul-bf16-v1"),
        bytes(range(256)) * 4,
        b"a" * 1000,
    ]
    for payload in payloads:
        rc, out, err = run_checker("sha256", payload)
        assert rc == 0, (rc, out, err)
        assert out == hashlib.sha256(payload).hexdigest() + "\n", out


def test_checker_input_bound() -> None:
    rc, out, err = run_checker("sha256", b"a" * (1024 * 1024 + 1))
    assert rc == 65, (rc, out, err)
    assert out == "", out


def test_json_mirrors() -> None:
    for name, want in CONTRACTS.items():
        path = SPEC / (name + ".json")
        data = json.loads(path.read_text())
        assert data["contract_id"] == want
        fields = {}
        for line in contract_text(name).splitlines()[1:]:
            key, value = line.split("=", 1)
            fields[key] = value
        assert set(data["fields"]) == set(fields), (name, set(data["fields"]), set(fields))
        for key, value in data["fields"].items():
            assert str(value) == fields[key], (name, key, value, fields[key])
        assert data["format"] == "omega-op-contract/1"


def test_checker_accepts_contracts() -> None:
    for name in CONTRACTS:
        checker_contract(name)


def _mutations(name: str = "matmul-bf16-v1") -> dict[str, tuple[str, str]]:
    text = contract_text(name)
    lines = text.splitlines(True)
    reordered = list(lines)
    reordered[3], reordered[4] = reordered[4], reordered[3]
    return {
        "reordered": ("".join(reordered), "E_ORDER"),
        "tolerance": (
            text.replace(
                "tolerance=abs<=1e-5*max(1,abs(ref))",
                "tolerance=abs<=1e-4*max(1,abs(ref))",
            ),
            "E_TOLERANCE",
        ),
        "dtype": (text.replace("dtype_a=bf16", "dtype_a=f16"), "E_DTYPE"),
        "layout": (text.replace("layout_a=row-major", "layout_a=col-major"), "E_LAYOUT"),
        "stride": (
            text.replace("stride_a=contiguous", "stride_a=strided"),
            "E_LAYOUT",
        ),
        "accumulation": (
            text.replace("accumulation=fp32", "accumulation=fp16"),
            "E_VALUE",
        ),
        "unknown": (text.replace("beta=0\n", "beta=0\nbogus=1\n"), "E_UNKNOWN"),
        "missing": (text.replace("beta=0\n", ""), "E_MISSING"),
        "header": (
            text.replace("omega-op-contract/1", "omega-op-contract/2", 1),
            "E_HEADER",
        ),
        "trailing": (text + "junk\n", "E_ORDER"),
        "trailing_key": (text + "foo=bar\n", "E_UNKNOWN"),
        "no_final_newline": (text[:-1], "E_ORDER"),
    }


def _gqa_mutations() -> dict[str, tuple[str, str]]:
    text = contract_text("gqa-causal-v1")
    lines = text.splitlines(True)
    reordered = list(lines)
    reordered[3], reordered[4] = reordered[4], reordered[3]
    return {
        "reordered": ("".join(reordered), "E_ORDER"),
        "tolerance": (
            text.replace(
                "tolerance_normal=rel<=2e-4,abs<=2e-5",
                "tolerance_normal=rel<=2e-3,abs<=2e-5",
            ),
            "E_TOLERANCE",
        ),
        "dtype": (text.replace("dtype_q=f32", "dtype_q=f16"), "E_DTYPE"),
        "layout": (
            text.replace("layout_q=token-head-dim", "layout_q=row-major"),
            "E_LAYOUT",
        ),
        "stride": (
            text.replace("kv_stride=token-major", "kv_stride=strided"),
            "E_LAYOUT",
        ),
        "value": (text.replace("scale=1/sqrt(128)", "scale=1/sqrt(64)"), "E_VALUE"),
        "unknown": (
            text.replace("alias=forbid\n", "alias=forbid\nbogus=1\n", 1),
            "E_UNKNOWN",
        ),
        "missing": (text.replace("alias=forbid\n", "", 1), "E_MISSING"),
        "header": (
            text.replace("omega-op-contract/1", "omega-op-contract/2", 1),
            "E_HEADER",
        ),
        "trailing": (text + "junk\n", "E_ORDER"),
        "trailing_key": (text + "foo=bar\n", "E_UNKNOWN"),
        "no_final_newline": (text[:-1], "E_ORDER"),
    }


def _mutation_corpora() -> list[tuple[str, dict[str, tuple[str, str]]]]:
    return [
        ("matmul-bf16-v1", _mutations("matmul-bf16-v1")),
        ("gqa-causal-v1", _gqa_mutations()),
    ]


def test_checker_rejects_mutations() -> None:
    for name, corpus in _mutation_corpora():
        for label, (mutated, code) in corpus.items():
            try:
                checker_contract_rejects(mutated, code)
            except AssertionError as exc:
                raise AssertionError(f"{name}/{label}: {exc}") from None


def test_validator_rejects_mutations() -> None:
    for name, corpus in _mutation_corpora():
        for label, (mutated, code) in corpus.items():
            got = validate_contract(mutated)
            assert got == code, (name, label, got, code)


def test_validator_accepts_contracts() -> None:
    for name in CONTRACTS:
        assert validate_contract(contract_text(name)) is None


def test_matmul_known_2x2() -> None:
    a = [oracle.bf16_bits(float(x)) for x in (1, 2, 3, 4)]
    b = [oracle.bf16_bits(float(x)) for x in (5, 6, 7, 8)]
    ref = oracle.matmul_bf16_ref(a, b, 2, 2, 2)
    assert ref == [19.0, 22.0, 43.0, 50.0], ref
    assert oracle.matmul_matches(oracle.matmul_fp32_output(ref), ref)


def test_matmul_known_1x1() -> None:
    a = [oracle.bf16_bits(2.0)]
    b = [oracle.bf16_bits(3.0)]
    assert oracle.matmul_bf16_ref(a, b, 1, 1, 1) == [6.0]


def test_bf16_conversion() -> None:
    exact = [0.0, 1.0, -2.5, 3.984375, -0.015625, 255.0 / 64.0, -255.0 / 64.0]
    for value in exact:
        bits = oracle.bf16_bits(value)
        assert oracle.bf16_from_bits(bits) == value, value
    # RNE ties: 1 + 1/256 is halfway between 1 and 1 + 1/128.
    assert oracle.bf16_bits(oracle.f32_from_bits(0x3F808000)) == 0x3F80
    assert oracle.bf16_bits(oracle.f32_from_bits(0x3F818000)) == 0x3F82
    for bad in (math.inf, -math.inf, math.nan):
        try:
            oracle.bf16_bits(bad)
        except ValueError:
            pass
        else:
            raise AssertionError(f"bf16 accepted {bad}")


def _attention_inputs(
    seed: int, r: int, base: int, hq: int, hkv: int, head_dim: int = oracle.HEAD_DIM
):
    rng = oracle.SplitMix64(seed)
    t = base + r
    q = [oracle.fp32(rng.next_f32_value()) for _ in range(r * hq * head_dim)]
    k = [oracle.fp32(rng.next_f32_value()) for _ in range(t * hkv * head_dim)]
    v = [oracle.fp32(rng.next_f32_value()) for _ in range(t * hkv * head_dim)]
    return q, k, v


def test_attention_single_token() -> None:
    q, k, v = _attention_inputs(1, 1, 0, 1, 1)
    ref = oracle.attention_ref(q, k, v, 0, 1, 1)
    assert ref == v


def test_attention_causal_boundary() -> None:
    d = oracle.HEAD_DIM
    r, base, hq, hkv = 2, 0, 1, 1
    t = base + r
    q = [0.0] * (r * hq * d)
    k = [0.0] * (t * hkv * d)
    v = [0.0] * (t * hkv * d)
    q[0] = 1.0
    q[d] = 1.0
    k[0] = 1.0
    k[d] = 1.0e6
    v[0] = 7.0
    v[d] = 9.0
    ref = oracle.attention_ref(q, k, v, base, hq, hkv, d)
    row0 = ref[0:d]
    assert row0[1:] == [0.0] * (d - 1)
    # Row 0 attends token 0 only; token 1 is masked even with a huge score.
    assert row0[0] == 7.0
    # Row 1 sees token 0 and token 1 and is dominated by token 1.
    assert ref[d] == 9.0


def test_attention_zero_length() -> None:
    d = oracle.HEAD_DIM
    assert oracle.attention_ref([], [], [], 0, 2, 1, d) == []
    zeros = oracle.attention_ref([0.0] * (2 * 2 * d), [], [], 0, 2, 1, d)
    assert zeros == [0.0] * (2 * 2 * d)


def test_attention_head_map() -> None:
    d = oracle.HEAD_DIM
    r, base, hq, hkv = 1, 0, 4, 2
    q = [1.0] * (r * hq * d)
    k = [1.0] * (1 * hkv * d)
    v = [0.0] * (1 * hkv * d)
    for i in range(d):
        v[i] = 1.0
        v[d + i] = 2.0
    ref = oracle.attention_ref(q, k, v, base, hq, hkv, d)
    for h in range(hq):
        want = 1.0 if h // (hq // hkv) == 0 else 2.0
        assert ref[h * d] == want, (h, ref[h * d], want)


def _matmul_fp32_accumulation(
    a_bits: list[int], b_bits: list[int], m: int, k: int, n: int
) -> list[float]:
    a = [oracle.bf16_from_bits(x) for x in a_bits]
    b = [oracle.bf16_from_bits(x) for x in b_bits]
    out = []
    for i in range(m):
        for j in range(n):
            acc = 0.0
            for p in range(k):
                acc = oracle.fp32(acc + oracle.fp32(a[i * k + p] * b[p * n + j]))
            out.append(acc)
    return out


def test_mutation_non_causal_mask() -> None:
    q, k, v = _attention_inputs(11, 2, 1, 1, 1)
    causal = oracle.attention_ref(q, k, v, 1, 1, 1)
    non_causal = oracle.attention_ref(q, k, v, 1, 1, 1, causal=False)
    assert causal != non_causal
    assert not oracle.attention_matches(non_causal, causal)
    assert not oracle.attention_matches_large(non_causal, causal)


def test_mutation_wrong_head_map() -> None:
    q, k, v = _attention_inputs(12, 1, 0, 4, 2)
    right = oracle.attention_ref(q, k, v, 0, 4, 2, head_map="div")
    wrong = oracle.attention_ref(q, k, v, 0, 4, 2, head_map="mod")
    assert right != wrong
    assert not oracle.attention_matches(wrong, right)


def test_mutation_wrong_stride() -> None:
    a = [oracle.bf16_bits(float(x)) for x in (1, 2, 3, 4)]
    b = [oracle.bf16_bits(float(x)) for x in (5, 6, 7, 8)]
    ref = oracle.matmul_bf16_ref(a, b, 2, 2, 2)
    # B read with the wrong (column-major) stride is B transposed.
    transposed = [
        b[0],
        b[2],
        b[1],
        b[3],
    ]
    wrong = oracle.matmul_bf16_ref(a, transposed, 2, 2, 2)
    assert wrong != ref
    assert not oracle.matmul_matches(oracle.matmul_fp32_output(wrong), ref)


def test_mutation_fp32_accumulation() -> None:
    k = 8192
    big = 2.0**26
    a = [oracle.bf16_bits(1.0)] * k
    b = [oracle.bf16_bits(1.0)] * k
    a[0] = oracle.bf16_bits(big)
    a[-1] = oracle.bf16_bits(-big)
    ref = oracle.matmul_bf16_ref(a, b, 1, k, 1)
    wrong = _matmul_fp32_accumulation(a, b, 1, k, 1)
    assert abs(ref[0]) > 8000.0, ref
    assert abs(wrong[0]) < 8.0, wrong
    assert not oracle.matmul_matches(oracle.matmul_fp32_output(wrong), ref)


def test_mutation_changed_tolerance_and_unknown_field() -> None:
    text = contract_text("matmul-bf16-v1")
    mutated = text.replace(
        "tolerance=abs<=1e-5*max(1,abs(ref))",
        "tolerance=abs<=1e-2*max(1,abs(ref))",
    )
    assert validate_contract(mutated) == "E_TOLERANCE"
    checker_contract_rejects(mutated, "E_TOLERANCE")
    unknown = text + "bogus=1\n"
    assert validate_contract(unknown) == "E_UNKNOWN"
    checker_contract_rejects(unknown, "E_UNKNOWN")


def test_manifest_accepts() -> None:
    payload = contract_bytes("matmul-bf16-v1")
    text = manifest_text(payload, oracle.MATMUL_CONTRACT_ID, "matmul", "matmul-v1")
    checker_manifest(text, oracle.MATMUL_CONTRACT_ID, None)
    assert validate_manifest(text, oracle.MATMUL_CONTRACT_ID) is None
    no_payload = manifest_text(
        payload, oracle.MATMUL_CONTRACT_ID, "matmul", "matmul-v1", with_payload=False
    )
    checker_manifest(no_payload, oracle.MATMUL_CONTRACT_ID, None)


def test_manifest_mutations() -> None:
    payload = contract_bytes("matmul-bf16-v1")
    good = manifest_text(payload, oracle.MATMUL_CONTRACT_ID, "matmul", "matmul-v1")
    cases: list[tuple[str, str, str | None]] = [
        (
            "stale",
            manifest_text(
                payload,
                oracle.MATMUL_CONTRACT_ID,
                "matmul",
                "matmul-v1",
                expected=oracle.GQA_CONTRACT_ID,
            ),
            "E_TARGET",
        ),
        (
            "foreign_op",
            manifest_text(
                payload, oracle.GQA_CONTRACT_ID, "matmul", "matmul-v1"
            ),
            "E_TARGET",
        ),
        (
            "bad_checksum",
            good.replace(
                "input_sha256=" + hashlib.sha256(payload).hexdigest(),
                "input_sha256=" + "0" * 64,
            ),
            "E_VALUE",
        ),
        ("unknown", good + "bogus=1\n", "E_UNKNOWN"),
        (
            "missing",
            good.replace("seed=680\n", ""),
            "E_MISSING",
        ),
        (
            "order",
            good.replace(
                f"op=matmul\ncontract={oracle.MATMUL_CONTRACT_ID}",
                f"contract={oracle.MATMUL_CONTRACT_ID}\nop=matmul",
            ),
            "E_ORDER",
        ),
        ("bad_decimal", good.replace("count=4\n", "count=4294967296\n"), "E_VALUE"),
        (
            "bad_hex",
            good.replace(
                oracle.MATMUL_CONTRACT_ID, "zz" + oracle.MATMUL_CONTRACT_ID[2:]
            ),
            "E_VALUE",
        ),
    ]
    for label, text, code in cases:
        checker_manifest(text, "", code)
        got = validate_manifest(text, oracle.MATMUL_CONTRACT_ID)
        assert got == code, (label, got, code)


def _fixture_jobs() -> list[tuple[Path, dict]]:
    return [
        (FIXDIR / "matmul-v1.json", oracle.make_matmul_fixture()),
        (FIXDIR / "gqa-causal-v1.json", oracle.make_attention_fixture()),
    ]


def write_fixtures() -> None:
    FIXDIR.mkdir(parents=True, exist_ok=True)
    for path, fixture in _fixture_jobs():
        path.write_bytes(oracle.fixture_bytes(fixture))


def test_fixtures_regenerate() -> None:
    for path, fixture in _fixture_jobs():
        assert path.exists(), f"missing fixture {path}; run --write-fixtures"
        assert path.read_bytes() == oracle.fixture_bytes(fixture), path


def test_fixture_checksums() -> None:
    for path, _ in _fixture_jobs():
        data = json.loads(path.read_text())
        for key, bits in data["inputs"].items():
            assert oracle.input_checksum(bits) == data["input_sha256"][key], (
                data["name"],
                key,
            )


def test_fixture_oracle_matches() -> None:
    matmul = json.loads((FIXDIR / "matmul-v1.json").read_text())
    shape = matmul["shape"]
    a = oracle.parse_bits16(matmul["inputs"]["a"])
    b = oracle.parse_bits16(matmul["inputs"]["b"])
    ref = oracle.matmul_bf16_ref(a, b, shape["m"], shape["k"], shape["n"])
    got = oracle.bits_hex32([oracle.f32_bits(oracle.fp32(x)) for x in ref])
    assert got == matmul["reference"]["c"]

    attn = json.loads((FIXDIR / "gqa-causal-v1.json").read_text())
    shape = attn["shape"]
    q = [oracle.f32_from_bits(x) for x in oracle.parse_bits32(attn["inputs"]["q"])]
    k = [oracle.f32_from_bits(x) for x in oracle.parse_bits32(attn["inputs"]["k"])]
    v = [oracle.f32_from_bits(x) for x in oracle.parse_bits32(attn["inputs"]["v"])]
    ref = oracle.attention_ref(
        q,
        k,
        v,
        shape["base"],
        shape["hq"],
        shape["hkv"],
        shape["head_dim"],
    )
    got = oracle.bits_hex32([oracle.f32_bits(oracle.fp32(x)) for x in ref])
    assert got == attn["reference"]["out"]


TESTS = [
    ("contract_ids_match_sha256sum", test_contract_ids_match_sha256sum),
    ("checker_contract_ids", test_checker_contract_ids),
    ("checker_sha256", test_checker_sha256),
    ("checker_input_bound", test_checker_input_bound),
    ("json_mirrors", test_json_mirrors),
    ("checker_accepts_contracts", test_checker_accepts_contracts),
    ("checker_rejects_mutations", test_checker_rejects_mutations),
    ("validator_rejects_mutations", test_validator_rejects_mutations),
    ("validator_accepts_contracts", test_validator_accepts_contracts),
    ("matmul_known_2x2", test_matmul_known_2x2),
    ("matmul_known_1x1", test_matmul_known_1x1),
    ("bf16_conversion", test_bf16_conversion),
    ("attention_single_token", test_attention_single_token),
    ("attention_causal_boundary", test_attention_causal_boundary),
    ("attention_zero_length", test_attention_zero_length),
    ("attention_head_map", test_attention_head_map),
    ("mutation_non_causal_mask", test_mutation_non_causal_mask),
    ("mutation_wrong_head_map", test_mutation_wrong_head_map),
    ("mutation_wrong_stride", test_mutation_wrong_stride),
    ("mutation_fp32_accumulation", test_mutation_fp32_accumulation),
    (
        "mutation_changed_tolerance_and_unknown_field",
        test_mutation_changed_tolerance_and_unknown_field,
    ),
    ("manifest_accepts", test_manifest_accepts),
    ("manifest_mutations", test_manifest_mutations),
    ("fixtures_regenerate", test_fixtures_regenerate),
    ("fixture_checksums", test_fixture_checksums),
    ("fixture_oracle_matches", test_fixture_oracle_matches),
]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--write-fixtures",
        action="store_true",
        help="regenerate the committed deterministic fixtures",
    )
    args = parser.parse_args(argv)
    load_schemas()
    if args.write_fixtures:
        write_fixtures()
        print("wrote fixtures under", FIXDIR)

    results = []
    passed = 0
    failed = 0
    for name, fn in TESTS:
        try:
            fn()
        except Exception as exc:  # noqa: BLE001 - the summary must name failures
            failed += 1
            results.append({"test": name, "status": "FAIL", "detail": str(exc)})
            print(f"TEST {name} FAIL {exc}")
        else:
            passed += 1
            results.append({"test": name, "status": "PASS"})
            print(f"TEST {name} PASS")
    summary = {"total": len(TESTS), "pass": passed, "fail": failed}
    print("SUMMARY " + json.dumps(summary, sort_keys=True))
    print(json.dumps({"tests": results, "summary": summary}, sort_keys=True))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
