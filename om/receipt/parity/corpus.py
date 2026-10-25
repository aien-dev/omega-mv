#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""corpus.py OUTDIR: write the receipt parity corpus, one record stream per file (OUTDIR/<stream>.bin),
and print "<stream> <records>" per stream.

A record is an op byte, then the payload length, a and b as 8 decimal digits each, then the payload
(see the harness notes at the end of om/receipt/receipt.om). Everything is deterministic: the
random parts use fixed seeds, so the same corpus comes out on every run. The three Rust-written
receipts are read from tests/resolve/receipts/ of the checkout this file lives in.
"""
import json
import os
import random
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
ZERO = "0" * 64
RUST = ["54f597d22b3753e47d6bd7706b6a7a422c189024912ec4626347e860b5f44a8c",
        "7fdff014cb20a0af5b5a5b377532292be29fc15da36d434944ad4521e69de5b3",
        "8e885832de2e7123555c47803547161c02196646b270d41078d8b16d4beaf765"]
TIERS = ["TEST_ONLY_TRUST", "HOST_TEST", "QEMU", "QEMU_SECURITY", "MACHINE1_READ_ONLY", "MACHINE1_ATTENDED",
         "MACHINE1_MUTATING", "PRODUCTION"]
MUTS = ["NONE", "VOLATILE_ONLY", "REMOVABLE_MEDIA_ONLY", "ONE_TIME_BOOT_SELECTION", "BOUNDED_TEST_REGION_WRITE",
        "BOOT_CONFIGURATION_CHANGE", "TRUST_ROOT_CHANGE", "TPM_POLICY_CHANGE", "DESTRUCTIVE_STORAGE"]
MAX = 4 * 1024 * 1024


def rec(op, payload, a=0, b=0):
    if isinstance(payload, str):
        payload = payload.encode()
    return op.encode() + b"%08d%08d%08d" % (len(payload), a, b) + payload


def js(s):
    """a JSON string literal for s (ASCII escapes for non-ASCII, like serde would accept)"""
    return json.dumps(s)


# ---- the receipt shape of tests/test_omega_resolve.c (rj_emit), as an ordered list of (key, raw JSON)
def rj(kind="host-v1", tier="HOST_TEST", env_class=None, result="PASS", schema="EvidenceReceiptV1",
       decl="NONE", obs="NONE", procedure="fixture", reserved="", feature=None, version=1, inputs=(),
       deps=(), root="6" * 64, assert_pass=True, extra=False, dup=False, lease=False, no_assertion=False,
       dirty=False, authority="", idhex=ZERO):
    f = [("schema", js(schema)), ("version", str(version)), ("id", js(idhex)), ("kind", js(kind))]
    if dup:
        f.append(("kind", js(kind)))
    if extra:
        f.append(("surprise", "1"))
    f += [("tier", js(tier)), ("result", js(result)), ("timestamp", "1786000000"), ("repo", js("fixture")),
          ("commit", js("a" * 40)), ("dirty", "true" if dirty else "false"), ("toolchain", js("fixture")),
          ("procedure", js(procedure)), ("machine", js("host")), ("env_class", js(env_class or tier)),
          ("input_artifacts", "[" + ", ".join(js(x) for x in inputs) + "]"), ("output_artifacts", "[]")]
    if no_assertion:
        f.append(("assertions", "[]"))
    else:
        f.append(("assertions", '[{"id": "fixture_check", "expected": "pass", "observed": "%s", "pass": %s, '
                  '"source": "test", "note": ""}]' % ("pass" if assert_pass else "fail",
                                                       "true" if assert_pass else "false")))
    f += [("dependencies", "[" + ", ".join(js(x) for x in deps) + "]"), ("declared_mutation", js(decl)),
          ("observed_mutation", js(obs)), ("authority", js(authority)), ("output_digest", js(root)),
          ("external_refs", "[]"), ("ledger", "null"),
          ("lease", '{"hold_id": "%s", "resource": "fixture-lease"}' % ("1" * 64) if lease else "null"),
          ("required_features", "[%s]" % js(feature) if feature is not None else "[]"),
          ("reserved", js(reserved))]
    return f


def emit(fields, sep=",\n", kv=": ", head="{\n", tail="\n}\n"):
    return head + sep.join(js(k) + kv + v for k, v in fields) + tail


def setf(fields, key, raw):
    return [(k, raw if k == key else v) for k, v in fields]


def dropf(fields, key):
    return [(k, v) for k, v in fields if k != key]


def sha(c):
    return "sha256:" + c * 64


def full():
    """a receipt that uses every field: ledger, lease, lists, several assertions, unicode"""
    f = rj(inputs=[sha("1"), sha("2"), "blake3:" + "ab" * 32, "C" * 64],
           deps=["8e885832de2e7123555c47803547161c02196646b270d41078d8b16d4beaf765",
                 "54F597D22B3753E47D6BD7706B6A7A422C189024912EC4626347E860B5F44A8C"])
    f = setf(f, "output_artifacts", "[%s, %s]" % (js("sha512:" + "e" * 128), js("blake3:" + "0f" * 16)))
    f = setf(f, "assertions", '[{"id": "zeta", "expected": "1", "observed": "1", "pass": true, "source": "s", '
             '"note": "n\\u00e9"}, {"id": "alpha", "pass": false}, {"id": "\\u03b1lpha", "expected": "x", '
             '"pass": true, "note": "\\ud83d\\ude00"}]')
    f = setf(f, "external_refs", '["https://example.invalid/b", "https://example.invalid/a", '
             '"https://example.invalid/a"]')
    f = setf(f, "ledger", '{"index": 18446744073709551615, "hash": "%s"}' % ("Ab" * 32))
    f = setf(f, "lease", '{"resource": " r ", "hold_id": "%s"}' % ("c" * 64))
    f = setf(f, "required_features", '["v1-base", "v1-base"]')
    f = setf(f, "kind", js("kind \u00e9\u4e2d\U0001F600 \"q\" \\ / \b\f\n\r\t"))
    return f


def s_rust():
    out = []
    for h in RUST:
        raw = open(os.path.join(ROOT, "tests", "resolve", "receipts", h + ".json"), "rb").read()
        out.append(rec("R", raw))
        out.append(rec("R", raw.replace(h.encode(), h.upper().encode())))
        doc = json.loads(raw)
        out.append(rec("R", json.dumps(doc, separators=(",", ":"))))
        out.append(rec("R", json.dumps(doc, indent=4)))
        out.append(rec("R", json.dumps(dict(reversed(list(doc.items()))))))
        out.append(rec("R", json.dumps(doc, ensure_ascii=True).replace('"PASS"', '"P\\u0041SS"')))
    return out


def shape_variants():
    v = [rj(), rj(dup=True), rj(extra=True), rj(schema="EvidenceReceiptV2"), rj(version=2),
         rj(feature="v2-extension"), rj(feature="v1-base"), rj(reserved="x"), rj(env_class="QEMU"),
         rj(obs="VOLATILE_ONLY"), rj(decl="VOLATILE_ONLY", obs="VOLATILE_ONLY"), rj(no_assertion=True),
         rj(no_assertion=True, result="FAIL"), rj(tier="PRODUCTION", kind="production-v1", authority="release-key-1"),
         rj(lease=True), rj(dirty=True), rj(assert_pass=False, result="FAIL"),
         rj(inputs=[sha("1"), sha("2")]), rj(inputs=[sha("2"), sha("1")]), rj(inputs=[sha("1"), sha("2")], procedure="another procedure"),
         rj(deps=RUST[:2]), rj(deps=[RUST[1], RUST[0], RUST[1]])]
    for a in ["", " ", "   ", "\t", "TEST-ONLY signer", "a test_only key", "TestOnly", "test-only", "TEST_ONLY"]:
        v.append(rj(tier="PRODUCTION", kind="production-v1", authority=a))
    return v


def s_shapes():
    out = []
    for f in shape_variants():
        d = emit(f)
        out += [rec("R", d), rec("M", d), rec("M", d, 0, 257)]
    d = emit(full())
    out += [rec("R", d), rec("M", d), rec("M", d, 0, 257)]
    return out


BADVALS = ["1", "-1", "1.5", "1e3", "\"x\"", "\"\"", "[]", "[1]", "[\"x\"]", "{}", "null", "true", "false",
           "18446744073709551615", "18446744073709551616", "0", "-0"]


def s_fields():
    out = []
    base = rj()
    keys = [k for k, _ in base]
    for k in keys:
        out.append(rec("M", emit(dropf(base, k))))
        for bv in BADVALS:
            out.append(rec("M", emit(setf(base, k, bv))))
    for k in ["schema", "kind", "tier", "repo", "commit", "toolchain", "procedure", "machine", "env_class",
              "authority", "reserved"]:
        for s in ["", " ", "\\u0041", "h\\u00e9llo", "\\ud83d\\ude00", "\\/\\\\\\\"", "raw \u00e9\u4e2d\U0001F600"]:
            out.append(rec("M", emit(setf(base, k, '"%s"' % s))))
    for n in ["0", "1", "2", "01", "00", "4294967295", "4294967296", "18446744073709551615",
              "18446744073709551616", "99999999999999999999", "100000000000000000000", "1.0", "1e0", "1E0",
              "-1", "-0", "1x", "1.", "1e+-.5", "  7  "]:
        out.append(rec("M", emit(setf(base, "version", n))))
        out.append(rec("M", emit(setf(base, "timestamp", n))))
    for c in ["a" * 40, "a" * 64, "a" * 39, "a" * 41, "A" * 40, "g" * 40, "0123456789abcdef" * 4, "\\u0061" * 40, "a" * 63]:
        out.append(rec("M", emit(setf(base, "commit", js(c) if "\\" not in c else '"%s"' % c))))
    for t in TIERS + ["host_test", "HOST_TEST ", "", "PRODUCTIO", "PRODUCTIONS"]:
        out.append(rec("M", emit(setf(setf(base, "tier", js(t)), "env_class", js(t)))))
        out.append(rec("M", emit(setf(base, "tier", js(t)))))
    for r in ["PASS", "FAIL", "BLOCKED", "INCOMPLETE", "SKIPPED", "pass", "", "PASSED"]:
        out.append(rec("M", emit(setf(base, "result", js(r)))))
        out.append(rec("M", emit(setf(setf(base, "result", js(r)), "assertions", "[]"))))
    # the mutation and tier rules: every declared x observed x tier
    for t in TIERS:
        for d in MUTS:
            for o in MUTS:
                f = rj(tier=t, decl=d, obs=o, authority="release-key" if t == "PRODUCTION" else "")
                out.append(rec("M", emit(f)))
    for m in ["none", "", "NONE ", "DESTRUCTIVE"]:
        out.append(rec("M", emit(setf(base, "declared_mutation", js(m)))))
        out.append(rec("M", emit(setf(base, "observed_mutation", js(m)))))
    digests = [sha("1"), "sha512:" + "a" * 128, "blake3:" + "a" * 32, "blake3:" + "a" * 31, "blake3:" + "a" * 33,
               "blake3:" + "A" * 34, "blake3:", "blake3:" + "g" * 32, "blake3:" + "a" * 32 + ":", "md5:" + "a" * 32,
               "sha256x:" + "a" * 64, "sha25:" + "a" * 64, ":" + "a" * 64, "a" * 64, "A" * 64, "a" * 63, "a" * 65,
               "g" * 64, "", "BLAKE3:" + "a" * 64, "sha256::" + "a" * 64, "\\u0073ha256:" + "a" * 64]
    for d in digests:
        lit = '"%s"' % d
        out.append(rec("M", emit(setf(base, "input_artifacts", "[%s]" % lit))))
        out.append(rec("M", emit(setf(base, "output_artifacts", "[%s, %s]" % (js(sha("3")), lit)))))
    for h in ["a" * 64, "A" * 64, "a" * 63, "a" * 65, "g" * 64, "", "\\u0061" * 64]:
        lit = '"%s"' % h
        out.append(rec("M", emit(setf(base, "output_digest", lit))))
        out.append(rec("M", emit(setf(base, "dependencies", "[%s]" % lit))))
        out.append(rec("M", emit(setf(base, "ledger", '{"index": 3, "hash": %s}' % lit))))
        out.append(rec("M", emit(setf(base, "lease", '{"hold_id": %s, "resource": "x"}' % lit))))
    # the id field itself (R: the placeholder stays, verify reports the mismatch or the parse error)
    for h in ["a" * 63, "a" * 65, "g" * 64, "", "A" * 64]:
        out.append(rec("R", emit(setf(base, "id", '"%s"' % h))))
    for lg in ["null", "1", "\"x\"", "[]", "{}", '{"index": 1}', '{"hash": "%s"}' % ("a" * 64),
               '{"index": -1, "hash": "%s"}' % ("a" * 64), '{"index": 1, "hash": 5}',
               '{"index": 1, "hash": "%s", "x": 1}' % ("a" * 64), '{"index": 0, "hash": "%s"}' % ("a" * 64)]:
        out.append(rec("M", emit(setf(base, "ledger", lg))))
    for ls in ["null", "1", "[]", "{}", '{"hold_id": "%s"}' % ("1" * 64), '{"resource": "x"}',
               '{"hold_id": "%s", "resource": ""}' % ("1" * 64), '{"hold_id": "%s", "resource": " \\t\\n\\r"}' % ("1" * 64),
               '{"hold_id": "%s", "resource": " x"}' % ("1" * 64), '{"hold_id": "%s", "resource": 1}' % ("1" * 64),
               '{"hold_id": 1, "resource": "x"}', '{"hold_id": "%s", "resource": "x", "y": 2}' % ("1" * 64)]:
        out.append(rec("M", emit(setf(base, "lease", ls))))
    for a in ['[]', '[1]', '"x"', '{}', '[{}]', '[{"id": "a"}]', '[{"pass": true}]', '[{"id": "", "pass": true}]',
              '[{"id": "a", "pass": true}, {"id": "a", "pass": false}]', '[{"id": "a", "pass": 1}]',
              '[{"id": 1, "pass": true}]', '[{"id": "a", "pass": true, "extra": 1}]',
              '[{"id": "a", "pass": true, "note": null}]', '[{"id": "b", "pass": true}, {"id": "a", "pass": true}]',
              '[{"id": "a", "pass": true}, {"id": "\\u0061", "pass": true}]', '[{"id": "a", "pass": true}, 7]',
              '[{"id": "a", "pass": true, "expected": "e", "observed": "o", "source": "s", "note": "n"}]',
              '[{"note": "n", "source": "s", "pass": false, "observed": "o", "expected": "e", "id": "z"}]',
              '[{"id": "a", "pass": true, "expected": 1}]', '[{"id": "a", "pass": true, "observed": []}]',
              '[{"id": "a", "pass": true, "source": {}}]', '[{"id": "a", "pass": null}]']:
        out.append(rec("M", emit(setf(base, "assertions", a))))
        out.append(rec("M", emit(setf(setf(base, "assertions", a), "result", '"FAIL"'))))
    for fl in ['[]', '["v1-base"]', '["v1-base", "x"]', '["x"]', '[1]', '["v1-bas"]', '["v1-base "]', '"v1-base"']:
        out.append(rec("M", emit(setf(base, "required_features", fl))))
    for ex in ['[]', '["b", "a", "b", "", "\\u00e9", "\\u00e8", "z"]', '[1]', '"x"', 'null']:
        out.append(rec("M", emit(setf(base, "external_refs", ex))))
    # sorting: byte order of the decoded strings, multi-byte UTF-8 and escapes, duplicates
    rnd = random.Random(7)
    alpha = ["a", "b", "A", "Z", "0", ":", "~", "\u00e9", "\u4e2d", "\U0001F600", "\\u0062", "\\/"]
    for i in range(40):
        items = []
        for j in range(rnd.randint(0, 12)):
            items.append("".join(rnd.choice(alpha) for _ in range(rnd.randint(0, 5))))
        lit = "[" + ", ".join('"%s"' % x for x in items) + "]"
        out.append(rec("M", emit(setf(base, "external_refs", lit))))
        deps = [rnd.choice(RUST) for _ in range(rnd.randint(0, 6))]
        deps = [d.upper() if rnd.random() < 0.4 else d for d in deps]
        out.append(rec("M", emit(setf(base, "dependencies", "[" + ", ".join(js(d) for d in deps) + "]"))))
        asr = ", ".join('{"id": "%s", "pass": true, "note": "%d"}' % (x, k) for k, x in enumerate(dict.fromkeys(items)))
        out.append(rec("M", emit(setf(base, "assertions", "[" + asr + "]"))))
    for a in ["release-key", "", " ", " \t\r\n", "x", "test-only", "TEST-ONLY", "Test_Only", "testonly", "TESTONLY",
              "my-test-onl", "test only", "testonl", "xtest-onlyx", "\\u0074est-only", "t\\u0045st_only",
              "aaaaaaaaaatestonly", "tes\u00e9tonly", "\u00e9testonly"]:
        out.append(rec("M", emit(rj(tier="PRODUCTION", authority=a))))
        out.append(rec("M", emit(rj(tier="MACHINE1_MUTATING", authority=a))))
    out.append(rec("M", emit(dropf(rj(tier="PRODUCTION"), "authority"))))
    # formatting: compact, other whitespace, key order (canonical bytes do not depend on them)
    for f in [base, full()]:
        out.append(rec("M", emit(f, sep=",", kv=":", head="{", tail="}")))
        out.append(rec("M", emit(f, sep=" ,\r\n\t", kv=" :\t", head=" \n\t{ ", tail=" } \r\n ")))
        out.append(rec("M", emit(list(reversed(f)))))
    out.append(rec("M", emit(base) + " x"))
    out.append(rec("M", emit(base, tail="\n}\n,")))
    return out


JSON_CASES = [
    "", " ", "{", "}", "[", "]", "{}", "[]", "null", "true", "false", "nul", "tru", "fals", "0", "-", "-0", "01", "-01",
    "1.", "1e", "1x", "\"", "\"abc", "\"a\\", "\"\\x\"", "\"\\u12\"", "\"\\u12g4\"", "\"\\ud800\"", "\"\\ud800\\u0041\"",
    "\"\\ud800\\udc00\"", "\"\\udc00\"", "\"\\u0000\"", "\"\\ud800x\"", "\"\\ud800\\\"", "\"\\ud800\\udbff\"",
    "\"\\ud800\\ue000\"", "\"\x01\"", "\"\x1f\"", "\"\x7f\"", "\"\xc0\x80\"", "\"\xc2\x80\"", "\"\xc2\"", "\"\xe0\x80\x80\"",
    "\"\xe0\xa0\x80\"", "\"\xed\xa0\x80\"", "\"\xef\xbf\xbf\"", "\"\xf0\x80\x80\x80\"", "\"\xf0\x90\x80\x80\"",
    "\"\xf4\x8f\xbf\xbf\"", "\"\xf4\x90\x80\x80\"", "\"\xf5\x80\x80\x80\"", "\"\xff\"", "\"\xe2\x82\"", "\"\xf0\x9f\x98\"",
    "{\"a\"}", "{\"a\" 1}", "{\"a\":}", "{\"a\":1,}", "{\"a\":1 \"b\":2}", "{\"a\":1,\"a\":2}", "{\"a\":1,\"\\u0061\":2}",
    "{\"a\":{\"a\":1},\"b\":{\"a\":2,\"a\":3}}", "{1:2}", "{,}", "[1,]", "[,1]", "[1 2]", "[1,,2]", "{\"a\":[1,{\"b\":2}],\"c\":3}",
    "[[[[[[1]]]]]]", "[[[[[[[1]]]]]]]", "[[[[[[]]]]]]", "[[[[[[[]]]]]]]", "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{}}}}}}",
    "{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":1}}}}}}", "1 2", "{} {}", "{}x", "  {}  ", "\t\n\r{}\r\n\t", "{}\x00",
    "\x00", "\xef\xbb\xbf{}", "{\"a\":\"\\b\\f\\n\\r\\t\\/\\\\\\\"\"}", "{\"a\":1e5}", "{\"a\":-1.5e-3}", "{\"a\":1E+2}",
    "{\"a\":nul}", "{\"a\":truex}", "{\"a\":\"x\"}", "[\"a\",\"a\"]", "{\"\":1,\"\":2}", "{\"a\":1}", "[1]", "\"x\"", "7",
    "{\"schema\":1}", "{\"zzz\":1}", "[{}]", "{\"a\":[1,2,3,{\"b\":[4,{\"c\":5}]}]}", "{\"a\":01}", "{\"a\":-}",
    "{\"a\":.5}", "{\"a\":+1}", "{\"a\":\"\\u00\"}", "{\"a\":\"\\uD83D\\uDE00\"}", "{\"a\":\"\\uDE00\\uD83D\"}",
    "{\"a\" : 1 , \"b\" : [ ] , \"c\" : { } }", "{\"a\":1,\"b\":2,\"c\":3,\"b\":4}",
]


def fuzz_json(rnd, depth=0):
    r = rnd.random()
    if depth > 7 or r < 0.3:
        return rnd.choice(["1", "-2", "0", "3.5", "1e9", "true", "false", "null", "\"s\"", "\"\\u00e9\"",
                           "\"\\ud83d\\ude00\"", "\"a\\nb\"", "18446744073709551616", "\"k\""])
    if r < 0.65:
        return "[" + ",".join(fuzz_json(rnd, depth + 1) for _ in range(rnd.randint(0, 4))) + "]"
    keys = ["a", "b", "c", "\\u0061", "id", "\u00e9"]
    return "{" + ",".join('"%s":%s' % (rnd.choice(keys), fuzz_json(rnd, depth + 1))
                          for _ in range(rnd.randint(0, 4))) + "}"


def s_json():
    out = [rec("R", c.encode("latin-1")) for c in JSON_CASES]
    rnd = random.Random(11)
    for _ in range(1500):
        d = fuzz_json(rnd).encode()
        out.append(rec("R", d))
        if d:
            b = bytearray(d)
            p = rnd.randrange(len(b))
            b[p] = rnd.choice(b' {}[]:,"\\x0\x01\xff-')
            out.append(rec("R", bytes(b)))
    for _ in range(300):
        out.append(rec("R", bytes(rnd.randrange(256) for _ in range(rnd.randint(1, 40)))))
    big = "{" + ",".join('"k%d":%d' % (i, i) for i in range(3000)) + "}"
    out.append(rec("R", big))
    out.append(rec("R", big[:-1] + ',"k2999":1}'))
    return out


def s_mutants():
    out = []
    rnd = random.Random(1726)
    raws = [open(os.path.join(ROOT, "tests", "resolve", "receipts", h + ".json"), "rb").read() for h in RUST]
    # every position of one Rust receipt with its 0x20 bit flipped (case flips, quote/space swaps)
    r0 = raws[0]
    for p in range(len(r0)):
        b = bytearray(r0)
        b[p] ^= 0x20
        out.append(rec("R", bytes(b)))
    # every truncation of another
    r1 = raws[1]
    for n in range(len(r1)):
        out.append(rec("R", r1[:n]))
    # random single-byte flips of all three
    for _ in range(600):
        r = rnd.choice(raws)
        b = bytearray(r)
        p = rnd.randrange(len(b))
        b[p] ^= rnd.randint(1, 255)
        out.append(rec("R", bytes(b)))
    # receipts built here (id computed by the reader under test), then flipped or truncated
    bases = [emit(f) for f in shape_variants()[:4]] + [emit(full()), emit(rj(lease=True, inputs=[sha("9")]))]
    for _ in range(1200):
        d = rnd.choice(bases)
        if rnd.random() < 0.2:
            out.append(rec("M", d, rnd.randrange(len(d)), 256))
        else:
            out.append(rec("M", d, rnd.randrange(len(d)), rnd.randint(1, 255)))
    return out


def s_big():
    out = []
    many = [sha("%x" % (i % 16)) [:7] + "%064x" % (i * 2654435761) for i in range(4096)]
    out.append(rec("M", emit(setf(rj(), "input_artifacts", "[" + ", ".join(js(x) for x in many) + "]"))))
    out.append(rec("M", emit(setf(rj(), "input_artifacts", "[" + ", ".join(js(x) for x in many + [sha("f")]) + "]"))))
    out.append(rec("M", emit(setf(rj(), "external_refs", "[" + ", ".join(js(x) for x in many[::-1] + many[:100]) + "]"))))
    over = "[" + ", ".join(js(x) for x in many + [sha("f")]) + "]"
    for k in ["output_artifacts", "dependencies", "required_features"]:
        out.append(rec("M", emit(setf(rj(), k, over))))
    asr = ", ".join('{"id": "a%05d", "pass": true}' % ((i * 7919) % 4096) for i in range(4096))
    out.append(rec("M", emit(setf(rj(), "assertions", "[" + asr + "]"))))
    out.append(rec("M", emit(setf(rj(), "assertions", "[" + asr + ', {"id": "x", "pass": true}]'))))
    # canonical bytes over 1 MiB (several harness windows): 4096 long digests
    longd = ["sha512:" + ("%04x" % i) * 250 for i in range(4096)]
    out.append(rec("M", emit(setf(rj(), "output_artifacts", "[" + ", ".join(js(x) for x in longd) + "]"))))
    out.append(rec("M", emit(rj(tier="PRODUCTION", authority="k" * (1 << 20)))))
    out.append(rec("M", emit(rj(tier="PRODUCTION", authority="k" * (1 << 20) + "test-only"))))
    out.append(rec("M", emit(setf(rj(), "toolchain", '"' + "\u00e9" * 1500000 + '"'))))
    out.append(rec("M", emit(setf(rj(), "toolchain", '"' + "\\u4e2d" * 600000 + '"'))))
    d = emit(rj())
    out.append(rec("M", d + " " * (MAX - len(d))))
    out.append(rec("M", d + " " * (MAX - len(d) + 1)))
    out.append(rec("R", d + " " * (MAX - len(d) + 1)))
    out.append(rec("R", b""))
    out.append(rec("M", emit(setf(rj(), "timestamp", "1" + "0" * 2000000))))
    out.append(rec("M", emit(setf(rj(), "version", "1." + "0" * 1500000))))
    out.append(rec("R", "[" + "0," * 1500000 + "0]"))
    out.append(rec("R", " " * (MAX - 2) + "{}"))
    return out


def s_tier():
    out = [rec("T", t) for t in TIERS]
    out += [rec("T", t) for t in ["", "host_test", "HOST_TEST ", " HOST_TEST", "PRODUCTIO", "PRODUCTIONX", "QEMU_",
                                   "TEST_ONLY_TRUST\xe9", "x" * 300]]
    out.append(rec("S", b""))
    return out


STREAMS = [("rust", s_rust), ("shapes", s_shapes), ("fields", s_fields), ("json", s_json),
           ("mutants", s_mutants), ("big", s_big), ("tier", s_tier)]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    os.makedirs(sys.argv[1], exist_ok=True)
    for name, fn in STREAMS:
        recs = fn()
        with open(os.path.join(sys.argv[1], name + ".bin"), "wb") as f:
            for r in recs:
                f.write(r)
        print(name, len(recs))


if __name__ == "__main__":
    main()
