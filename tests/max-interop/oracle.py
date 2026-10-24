# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Independent pure-Python reference for the MAX interop contracts.

This module deliberately uses only the Python standard library.  It does not
call CUDApp, Omega, or any compiled helper: every number here is produced from
the contract semantics by ordinary Python code.

Conventions
-----------
* bf16 values are carried as unsigned 16-bit integers (their bit pattern).
* f32 values are carried as unsigned 32-bit integers (their bit pattern).
* matmul accumulates exact bf16 products in fp64 (Python float), in k order.
* attention accumulates fp64 scores and fp64 weighted sums, with causal
  inclusive masking and max-subtracted softmax.
"""

from __future__ import annotations

import hashlib
import json
import math
import struct

MATMUL_CONTRACT_ID = "647116fc1ba1e1394982fc8ba181ce565293f160a6b56afc0f02b2128a675704"
GQA_CONTRACT_ID = "003696e422bba7274877ba9e01a61076ea0f862f32760cbaa06c60f85a115359"

HEAD_DIM = 128
MATMUL_TOL = 1e-5
ATTN_REL_NORMAL = 2e-4
ATTN_ABS_NORMAL = 2e-5
ATTN_REL_LARGE = 1e-3
ATTN_ABS_LARGE = 1e-4


# ---------------------------------------------------------------------------
# bit-level helpers


def f32_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def f32_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def fp32(value: float) -> float:
    """Round a double to the nearest IEEE binary32 value (ties to even)."""
    return f32_from_bits(f32_bits(value))


def bf16_from_bits(bits: int) -> float:
    """Exact bf16 -> double conversion (bf16 is the high half of f32)."""
    return f32_from_bits((bits & 0xFFFF) << 16)


def bf16_bits(value: float) -> int:
    """Round a double to bf16 with round-to-nearest-even, rejecting Inf/NaN."""
    bits = f32_bits(value)
    if (bits & 0x7F800000) == 0x7F800000:
        raise ValueError("bf16: non-finite input")
    rounded = (bits + 0x7FFF + ((bits >> 16) & 1)) >> 16
    if (rounded & 0x7F80) == 0x7F80:
        raise ValueError("bf16: rounding overflow")
    return rounded & 0xFFFF


def bf16_is_finite(bits: int) -> bool:
    return ((bits >> 7) & 0xFF) != 0xFF


def bits_hex16(bits: list[int]) -> list[str]:
    return [f"0x{b:04x}" for b in bits]


def bits_hex32(bits: list[int]) -> list[str]:
    return [f"0x{b:08x}" for b in bits]


def parse_bits16(items: list[str]) -> list[int]:
    return [int(x, 16) for x in items]


def parse_bits32(items: list[str]) -> list[int]:
    return [int(x, 16) for x in items]


# ---------------------------------------------------------------------------
# matmul


def _flat_index(i: int, j: int, cols: int) -> int:
    return i * cols + j


def matmul_bf16_ref(
    a_bits: list[int], b_bits: list[int], m: int, k: int, n: int
) -> list[float]:
    """fp64 reference: C[i,j] = sum_p exact_bf16(A[i,p]) * exact_bf16(B[p,j])."""
    if m < 1 or k < 1 or n < 1:
        raise ValueError("matmul: dimensions must be positive")
    if len(a_bits) != m * k or len(b_bits) != k * n:
        raise ValueError("matmul: buffer length does not match shape")
    for bits in a_bits:
        if not bf16_is_finite(bits):
            raise ValueError("matmul: non-finite A")
    for bits in b_bits:
        if not bf16_is_finite(bits):
            raise ValueError("matmul: non-finite B")
    a = [bf16_from_bits(x) for x in a_bits]
    b = [bf16_from_bits(x) for x in b_bits]
    out: list[float] = [0.0] * (m * n)
    for i in range(m):
        for j in range(n):
            acc = 0.0
            for p in range(k):
                acc += a[_flat_index(i, p, k)] * b[_flat_index(p, j, n)]
            out[_flat_index(i, j, n)] = acc
    return out


def matmul_fp32_output(ref: list[float]) -> list[float]:
    return [fp32(x) for x in ref]


def matmul_error(out: list[float], ref: list[float]) -> float:
    if len(out) != len(ref):
        raise ValueError("matmul: output length does not match reference")
    worst = 0.0
    for got, want in zip(out, ref):
        if not math.isfinite(got):
            return math.inf
        err = abs(got - want)
        allowed = MATMUL_TOL * max(1.0, abs(want))
        if err > allowed:
            worst = max(worst, err / allowed)
    return worst


def matmul_matches(out: list[float], ref: list[float]) -> bool:
    return len(out) == len(ref) and matmul_error(out, ref) <= 1.0


# ---------------------------------------------------------------------------
# causal grouped-query attention


def _attention_shape(
    q: list[float], k: list[float], v: list[float], hq: int, hkv: int, head_dim: int
) -> tuple[int, int]:
    if hq < 1 or hkv < 1 or head_dim < 1:
        raise ValueError("attention: dimensions must be positive")
    if hq % hkv != 0:
        raise ValueError("attention: q_heads must be a multiple of kv_heads")
    if len(q) % (hq * head_dim) != 0:
        raise ValueError("attention: q length does not match heads")
    r = len(q) // (hq * head_dim)
    if len(k) != len(v) or len(k) % (hkv * head_dim) != 0:
        raise ValueError("attention: k/v length does not match heads")
    t = len(k) // (hkv * head_dim)
    return r, t


def attention_ref(
    q: list[float],
    k: list[float],
    v: list[float],
    base: int,
    hq: int,
    hkv: int,
    head_dim: int = HEAD_DIM,
    causal: bool = True,
    head_map: str = "div",
) -> list[float]:
    """fp64 reference for the rows/causal GQA variant.

    q is [R][Hq][D], k/v are [T][Hkv][D], flat.  For causal rows, query row i
    attends tokens 0..base+i inclusive; the contract shape requires T = base+R.
    `head_map='mod'` is the deliberate wrong variant used by the mutation test.
    """
    r, t = _attention_shape(q, k, v, hq, hkv, head_dim)
    for buf in (q, k, v):
        for x in buf:
            if not math.isfinite(x):
                raise ValueError("attention: non-finite input")
    if r == 0:
        return []
    if t == 0:
        return [0.0] * (r * hq * head_dim)
    if causal and t < base + r:
        raise ValueError("attention: T must be at least base + R")
    ratio = hq // hkv
    scale = 1.0 / math.sqrt(head_dim)
    out = [0.0] * (r * hq * head_dim)
    for i in range(r):
        if causal:
            allowed = base + i + 1
        else:
            allowed = t
        if allowed > t:
            raise ValueError("attention: mask exceeds T")
        for h in range(hq):
            if head_map == "div":
                hk = h // ratio
            elif head_map == "mod":
                hk = h % hkv
            else:
                raise ValueError("attention: unknown head map")
            scores: list[float] = []
            for tok in range(allowed):
                score = 0.0
                q_off = (i * hq + h) * head_dim
                k_off = (tok * hkv + hk) * head_dim
                for d in range(head_dim):
                    score += q[q_off + d] * k[k_off + d]
                scores.append(score * scale)
            top = max(scores)
            exps = [math.exp(s - top) for s in scores]
            total = sum(exps)
            acc = [0.0] * head_dim
            for tok, weight in enumerate(exps):
                p = weight / total
                v_off = (tok * hkv + hk) * head_dim
                for d in range(head_dim):
                    acc[d] += p * v[v_off + d]
            o_off = (i * hq + h) * head_dim
            out[o_off : o_off + head_dim] = acc
    return out


def attention_fp32_output(ref: list[float]) -> list[float]:
    return [fp32(x) for x in ref]


def attention_error(
    out: list[float], ref: list[float], rel: float, abs_tol: float
) -> float:
    if len(out) != len(ref):
        raise ValueError("attention: output length does not match reference")
    worst = 0.0
    for got, want in zip(out, ref):
        if not math.isfinite(got):
            return math.inf
        err = abs(got - want)
        allowed = max(abs_tol, rel * abs(want))
        if err > allowed:
            worst = max(worst, err / allowed)
    return worst


def attention_matches(
    out: list[float],
    ref: list[float],
    rel: float = ATTN_REL_NORMAL,
    abs_tol: float = ATTN_ABS_NORMAL,
) -> bool:
    return len(out) == len(ref) and attention_error(out, ref, rel, abs_tol) <= 1.0


def attention_matches_large(out: list[float], ref: list[float]) -> bool:
    return attention_matches(out, ref, ATTN_REL_LARGE, ATTN_ABS_LARGE)


# ---------------------------------------------------------------------------
# deterministic fixtures


MASK64 = (1 << 64) - 1


class SplitMix64:
    def __init__(self, seed: int):
        self.state = seed & MASK64

    def next_u64(self) -> int:
        self.state = (self.state + 0x9E3779B97F4A7C15) & MASK64
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
        return (z ^ (z >> 31)) & MASK64

    def next_bf16_value(self) -> float:
        # |numerator| <= 255 needs at most 8 significant bits, exactly bf16.
        return ((self.next_u64() % 511) - 255) / 64.0

    def next_f32_value(self) -> float:
        # |numerator| <= 2048 needs at most 12 significant bits, exactly f32.
        return ((self.next_u64() % 4097) - 2048) / 1024.0


def input_checksum(bits: list[str]) -> str:
    """Checksum over the canonical JSON form of one bit-string input array."""
    return hashlib.sha256(
        json.dumps(bits, separators=(",", ":")).encode("ascii")
    ).hexdigest()


def make_matmul_fixture(seed: int = 680) -> dict:
    rng = SplitMix64(seed)
    m, k, n = 4, 5, 3
    a_bits = [bf16_bits(rng.next_bf16_value()) for _ in range(m * k)]
    b_bits = [bf16_bits(rng.next_bf16_value()) for _ in range(k * n)]
    ref = matmul_bf16_ref(a_bits, b_bits, m, k, n)
    out_bits = [f32_bits(fp32(x)) for x in ref]
    a_hex = bits_hex16(a_bits)
    b_hex = bits_hex16(b_bits)
    return {
        "format": "omega-fixture/1",
        "name": "matmul-v1",
        "contract": "matmul-bf16-v1",
        "contract_id": MATMUL_CONTRACT_ID,
        "generator": {"algorithm": "splitmix64", "seed": seed},
        "shape": {"m": m, "k": k, "n": n},
        "dtype": {"a": "bf16", "b": "bf16", "c": "f32"},
        "layout": {"a": "row-major", "b": "row-major", "c": "row-major"},
        "checksum_format": "sha256 of json.dumps(bits, separators=(',', ':'))",
        "inputs": {"a": a_hex, "b": b_hex},
        "input_sha256": {
            "a": input_checksum(a_hex),
            "b": input_checksum(b_hex),
        },
        "reference": {"c": bits_hex32(out_bits)},
        "tolerance": "abs<=1e-5*max(1,abs(ref))",
    }


def make_attention_fixture(seed: int = 680) -> dict:
    rng = SplitMix64(seed ^ 0xA77E0710)
    r, base, hq, hkv, head_dim = 2, 1, 4, 2, HEAD_DIM
    t = base + r
    q_bits = [f32_bits(fp32(rng.next_f32_value())) for _ in range(r * hq * head_dim)]
    k_bits = [f32_bits(fp32(rng.next_f32_value())) for _ in range(t * hkv * head_dim)]
    v_bits = [f32_bits(fp32(rng.next_f32_value())) for _ in range(t * hkv * head_dim)]
    q = [f32_from_bits(x) for x in q_bits]
    k = [f32_from_bits(x) for x in k_bits]
    v = [f32_from_bits(x) for x in v_bits]
    ref = attention_ref(q, k, v, base, hq, hkv, head_dim)
    out_bits = [f32_bits(fp32(x)) for x in ref]
    q_hex = bits_hex32(q_bits)
    k_hex = bits_hex32(k_bits)
    v_hex = bits_hex32(v_bits)
    return {
        "format": "omega-fixture/1",
        "name": "gqa-causal-v1",
        "contract": "gqa-causal-v1",
        "contract_id": GQA_CONTRACT_ID,
        "generator": {"algorithm": "splitmix64", "seed": seed ^ 0xA77E0710},
        "shape": {
            "r": r,
            "base": base,
            "t": t,
            "hq": hq,
            "hkv": hkv,
            "head_dim": head_dim,
        },
        "dtype": {"q": "f32", "k": "f32", "v": "f32", "out": "f32"},
        "layout": "token-head-dim",
        "mask": "causal-inclusive",
        "checksum_format": "sha256 of json.dumps(bits, separators=(',', ':'))",
        "inputs": {"q": q_hex, "k": k_hex, "v": v_hex},
        "input_sha256": {
            "q": input_checksum(q_hex),
            "k": input_checksum(k_hex),
            "v": input_checksum(v_hex),
        },
        "reference": {"out": bits_hex32(out_bits)},
        "tolerance": {
            "normal": "rel<=2e-4,abs<=2e-5",
            "large_scores": "rel<=1e-3,abs<=1e-4",
        },
    }


def fixture_bytes(fixture: dict) -> bytes:
    return (json.dumps(fixture, indent=2, sort_keys=True) + "\n").encode("utf-8")
