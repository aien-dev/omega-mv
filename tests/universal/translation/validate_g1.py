#!/usr/bin/env python3
"""Independent G1 translation validation (#623) against spec/universal/translation-v1.md.

Five representations of every F1 positive and every accepted c_import_controls.py boundary are compared on
boundary and seeded random input vectors:
  oracle    the original C, compiled by cc (external oracle)
  reference this file's own decoder (from source-aware-v1.md) and bigint evaluator of the upper records
  export    `c-export` output, compiled by cc
  reimport  import(export): semantic identity per section 4 plus evaluation of the re-imported records
  native    `lower` -> pinned omc (function mode) -> pinned bridge generator -> linked caller -> omega_calc
The lowering and export tools are exercised only through their public commands; their sources are not read.
Exit: 0 PASS, 1 FAIL, 3 NOT_RUN (a tool or pin is absent or wrong; nothing is passed).
"""
from pathlib import Path
import argparse, hashlib, importlib.util, itertools, json, os, random, re, struct, subprocess, sys

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE / 'mutants'))
import operators as MUT  # noqa: E402

MASK = (1 << 64) - 1
CONTRACT = ('spec/universal/translation-v1.md', '063f1d630e31925876454129c0024539bea0c7eb17f7187bc0e7ef56790de86e')
FROZEN = {  # section 1 of the contract, plus the fixture/control files this validator reads
    'spec/universal/semantics.md': 'd4ed6148bfa97cc693d3fa4495e2d7e2e68aba04607a7e96dd9a9c54bf9073a8',
    'spec/universal/source-aware-v1.md': 'e0efbc717b61976614c1a1e9eee543284bcfb5410922d23b4ad70cb6a99615ec',
    'spec/universal/c-u64-import-v1.md': '84f06ce1ad1653bed08c940696b23d87ce22afb7ebb1b5c3e5970a23b3916012',
    'tests/universal/translation/fixtures-f1.json': 'da567a67f747596b1a7285f046119296f1d3b45d2d057053e5d8a084639c7c2d',
    'tests/universal/c_import_gate.py': 'aef6a0bed19205e2a4ed96263fccc23b16654d0cf2c1af382e78ed455db6b02b',
    'tests/universal/c_import_controls.py': '6991dc771d288357d8814c2de44663d0604d5cfebd6cba8d8374fa0aa9cfa652',
}
OMC_SHA = '734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
IMPORT_COMPILER_SHA = '191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'
IMPORT_INPUTS = ['src/source-aware/vendor/sha256.om', 'src/adapters/c-u64-import.om']
BRIDGE_COMMIT = '012860dbf26ee62a9838594b9dfd1e3e94a540a9'
SEMANTICS_DIGEST = bytes.fromhex(FROZEN['spec/universal/semantics.md'])
BOUNDARY = [0, 1, 2, (1 << 32) - 1, 1 << 32, (1 << 63) - 1, 1 << 63, MASK - 1, MASK]
RANDOM_PER_CASE = 64
SAMPLE_CAP = 2048
CC_FLAGS = ['-std=c17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
REPS = ['oracle', 'reference', 'export', 'reimport', 'native']
NAME = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')


class NotRun(Exception):
    pass


class Bad(Exception):
    pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------- independent decoder (source-aware-v1.md)

def decode_envelope(buf):
    """Canonical wire format: OMSB, version 1, u32 semantic length, u32 sidecar length, exact total."""
    if len(buf) < 13 or buf[:4] != b'OMSB' or buf[4] != 1:
        raise Bad('envelope header')
    ns, np_ = struct.unpack_from('<II', buf, 5)
    if len(buf) != 13 + ns + np_:
        raise Bad('envelope length')
    return buf[13:13 + ns], (buf[13 + ns:] if np_ else None)


def decode_s1(sem):
    """Semantic header + records, then the S1 membership rules of translation-v1.md section 2."""
    if len(sem) < 44 or sem[:4] != b'OMSA' or sem[4] != 1 or sem[7] != 1:
        raise Bad('semantic header')
    lang, prof, runtime, abi = sem[5], sem[6], sem[8], sem[9]
    count = struct.unpack_from('<H', sem, 10)[0]
    if len(sem) != 44 + 20 * count or not 1 <= count <= 64:
        raise Bad('operation count')
    if sem[12:44] != SEMANTICS_DIGEST:
        raise Bad('semantics digest')
    if (lang, prof, runtime, abi) != (1, 1, 0, 1):
        raise Bad('not S1 header %r' % ((lang, prof, runtime, abi),))
    ops, slots = [], []
    for k in range(count):
        d, v, op, t, attr, eff, alias, obl, a, b, imm = struct.unpack_from('<8BHHQ', sem, 44 + 20 * k)
        if (d, v, t, eff, alias) != (1, 1, 1, 0, 0):
            raise Bad('record %d fields' % k)
        if op == 9:
            if (attr, obl, a, b) != (0, 1, 65535, 65535) or imm > 5:
                raise Bad('record %d input' % k)
            slots.append(imm)
        elif op == 1:
            if (attr, obl, a, b) != (1, 0, 65535, 65535):
                raise Bad('record %d constant' % k)
        elif op in (2, 3, 4):
            if (attr, obl, imm) != (1, 1, 0) or a >= k or b >= k:
                raise Bad('record %d arithmetic' % k)
        else:
            raise Bad('record %d opcode %d' % (k, op))
        ops.append((op, a, b, imm))
    if sorted(slots) != list(range(len(slots))):
        raise Bad('input slots not a permutation')
    return ops


def decode_sidecar(side, sem, count, src):
    """OMSP v1 sidecar bound to the semantic bytes and the exact source."""
    if side is None or len(side) < 74 or side[:4] != b'OMSP' or side[4] != 1 or side[5] not in (0, 1):
        raise Bad('sidecar header')
    n = struct.unpack_from('<I', side, 6)[0]
    if side[10:42] != hashlib.sha256(sem).digest() or len(side) != 74 + 8 * count + n:
        raise Bad('sidecar binding')
    body = side[74 + 8 * count:]
    if side[42:74] != hashlib.sha256(body).digest():
        raise Bad('sidecar source digest')
    for k in range(count):
        lo, hi = struct.unpack_from('<II', side, 74 + 8 * k)
        if not lo <= hi <= n:
            raise Bad('span %d' % k)
    if src is not None and body != src:
        raise Bad('stored source differs from imported source')
    return body


def identity(sem):
    return sha(b'omega.source.semantic.v1' + sem)


def evaluate(ops, args):
    val = []
    for op, a, b, imm in ops:
        if op == 9:
            x = args[imm]
        elif op == 1:
            x = imm
        elif op == 2:
            x = val[a] + val[b]
        elif op == 3:
            x = val[a] - val[b]
        else:
            x = val[a] * val[b]
        val.append(x & MASK)
    return val[-1]


def normalized_root(ops):
    """Section 4: NORMALIZED iff n>=3, result n-2 is const 0 and n-1 is add(r, n-2) with r < n-2."""
    n = len(ops)
    if n >= 3 and ops[n - 2] == (1, 65535, 65535, 0) and ops[n - 1][0] == 2 and ops[n - 1][2] == n - 2 \
            and ops[n - 1][1] < n - 2:
        return ops[n - 1][1]
    return None


def arity(ops):
    return sum(1 for o in ops if o[0] == 9)


# ---------------------------------------------------------------- contract renderings (sections 5 and 6)

def render_expr(ops, k):
    op, a, b, imm = ops[k]
    if op == 9:
        return 'i%d' % imm, 3
    if op == 1:
        return '%dULL' % imm, 3
    p = 2 if op == 4 else 1
    (lt, lp), (rt, rp) = render_expr(ops, a), render_expr(ops, b)
    if lp < p:
        lt = '(' + lt + ')'
    if rp <= p:
        rt = '(' + rt + ')'
    return '%s %s %s' % (lt, {2: '+', 3: '-', 4: '*'}[op], rt), p


def render_c(ops, name):
    r = normalized_root(ops)
    expr = render_expr(ops, len(ops) - 1 if r is None else r)[0]
    n = arity(ops)
    params = ', '.join('uint64_t i%d' % s for s in range(n)) if n else 'void'
    return '#include <stdint.h>\nuint64_t %s(%s) { return %s; }\n' % (name, params, expr)


def c_bounds(ops, text):
    """The c-u64-import-v1 bounds of the contract rendering: bytes, expression parenthesis depth and the
    upper operation count its re-import would have (inputs, one record per operator and per constant
    occurrence, plus the synthetic pair)."""
    expr = text.split('{ return ', 1)[1]
    depth = best = 0
    for ch in expr:
        depth += {'(': 1, ')': -1}.get(ch, 0)
        best = max(best, depth)
    nodes = sum(1 for ch in expr if ch in '+-*') + len(re.findall(r'\d+ULL', expr))
    return {'bytes': len(text.encode()), 'depth': best, 'reimport_ops': arity(ops) + nodes + 2}


def render_om(ops, name, ident):
    n = arity(ops)
    sig = ''.join(' i%d ulong' % s for s in range(n))
    out = ['note omega.universal.lower.v1 semantic ' + ident, 'fn %s%s to ulong' % (name, sig),
           'v = new ulong %d 0' % len(ops)]
    for k, (op, a, b, imm) in enumerate(ops):
        if op == 9:
            out.append('put v %d i%d' % (k, imm))
        elif op == 1:
            out.append('put v %d %d' % (k, imm))
        else:
            out.append('put v %d %s get v %d get v %d' % (k, {2: 'add', 3: 'sub', 4: 'mul'}[op], a, b))
    out += ['ret get v %d' % (len(ops) - 1), 'end']
    return '\n'.join(out) + '\n'


# ---------------------------------------------------------------- envelopes built by this validator

def build_sem(ops, lang=1, prof=1, runtime=0, abi=1, fields=None):
    recs = b''
    for k, (op, a, b, imm) in enumerate(ops):
        if fields:
            f = fields(op)
        else:
            f = {9: (1, 0, 0, 0, 1), 1: (1, 1, 0, 0, 0)}.get(op, (1, 1, 0, 0, 1))
        t, attr, eff, alias, obl = f
        recs += struct.pack('<8BHHQ', lang, 1, op, t, attr, eff, alias, obl, a, b, imm)
    return b'OMSA' + bytes([1, lang, prof, 1, runtime, abi]) + struct.pack('<H', len(ops)) + SEMANTICS_DIGEST + recs


def envelope(sem, side=b''):
    return b'OMSB\x01' + struct.pack('<II', len(sem), len(side)) + sem + side


U = 65535


def IN(s):
    return (9, U, U, s)


def K(v):
    return (1, U, U, v)


def ADD(a, b):
    return (2, a, b, 0)


def SUB(a, b):
    return (3, a, b, 0)


def MUL(a, b):
    return (4, a, b, 0)


def probes():
    """Validator-authored S1 units (semantic-only) that F1 does not reach: record-order input slots,
    shared results, right nesting, units that are not NORMALIZED, and export LIMIT boundaries."""
    P = []
    P.append(('X00-permuted-slots', [IN(1), IN(0), SUB(0, 1)], 'ok'))
    P.append(('X01-shared-square', [IN(0), IN(1), ADD(0, 1), MUL(2, 2), K(0), ADD(3, 4)], 'ok'))
    P.append(('X02-three-input-tree', [IN(0), IN(1), IN(2), SUB(1, 2), ADD(0, 2), MUL(3, 4), SUB(0, 5),
                                       K(0), ADD(6, 7)], 'ok'))
    P.append(('X03-interleaved-unused', [K(5), IN(2), IN(0), IN(1), MUL(0, 1), SUB(4, 3), K(0), ADD(5, 6)], 'ok'))
    P.append(('X04-lone-constant', [K(42)], 'ok'))
    P.append(('X05-zero-on-left', [IN(0), K(0), ADD(1, 0)], 'ok'))
    P.append(('X06-plus-one-not-normal', [IN(0), IN(1), MUL(0, 1), K(1), ADD(2, 3)], 'ok'))
    P.append(('X07-double-normalized', [IN(0), K(0), ADD(0, 1), K(0), ADD(2, 3)], 'ok'))
    P.append(('X08-max-constants', [IN(0), K(MASK), MUL(0, 1), K(1 << 63), SUB(2, 3), K(0), ADD(4, 5)], 'ok'))
    P.append(('X09-root-is-input', [IN(0), IN(1), IN(2), IN(3), IN(4), IN(5), K(0), ADD(3, 6)], 'ok'))
    sub16 = [IN(0), IN(1)]
    for _ in range(17):
        sub16.append(SUB(0, len(sub16) - 1))
    P.append(('X10-depth-16-boundary', sub16, 'ok'))
    sub17 = sub16 + [SUB(0, len(sub16) - 1)]
    P.append(('X11-depth-17-over', sub17, 'limit'))
    exp = [IN(0)]
    for _ in range(12):
        exp.append(MUL(len(exp) - 1, len(exp) - 1))
    P.append(('X12-shared-expansion-bytes', exp, 'limit'))
    dbl = [IN(0), IN(1), MUL(0, 1)]
    for _ in range(5):
        dbl.append(ADD(len(dbl) - 1, len(dbl) - 1))
    P.append(('X13-reimport-ops-over-64', dbl, 'limit'))
    chain = [IN(0), IN(1), ADD(0, 1)]
    while len(chain) < 64:
        chain.append(ADD(len(chain) - 1, 1))
    P.append(('X14-64-records-not-normalized', chain, 'limit'))
    return P


def non_s1_envelopes(base):
    """Units valid under source-aware v1 that are outside S1 (section 2): expected 68."""
    b = [o for o in base]
    E = []
    E.append(('rust-profile-2', build_sem(b, lang=2, prof=2)))
    E.append(('python-explicit-profile-4', build_sem(b, lang=3, prof=4, fields=lambda op: {9: (1, 0, 0, 0, 1), 1: (1, 1, 0, 0, 0)}.get(op, (1, 1, 0, 0, 5)))))
    E.append(('osc-profile-5', build_sem(b, lang=4, prof=5)))
    div = [IN(0), IN(1), (5, 0, 1, 0), K(0), ADD(2, 3)]
    E.append(('c17-division', build_sem(div, fields=lambda op: {9: (1, 0, 0, 0, 1), 1: (1, 1, 0, 0, 0), 5: (1, 1, 0, 0, 3)}.get(op, (1, 1, 0, 0, 1)))))
    return E


def malformed_envelopes(env):
    """Framing violations of the OMSB/OMSA wire format: expected 65."""
    sem, _ = decode_envelope(env)
    E = [('empty', b''), ('magic', b'OMSX' + env[4:]), ('truncated', env[:-1]), ('trailing-byte', env + b'\0'),
         ('short-header', env[:12]), ('semantic-length-over', env[:5] + struct.pack('<I', len(sem) + 1) + env[9:]),
         ('record-count-over', envelope(sem[:10] + struct.pack('<H', struct.unpack_from('<H', sem, 10)[0] + 1) + sem[12:])),
         ('record-cut', envelope(sem[:-1]))]
    return E


# ---------------------------------------------------------------- process helpers

def run(argv, data=b'', timeout=60):
    try:
        r = subprocess.run([str(x) for x in argv], input=data, capture_output=True, timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return 'timeout', b'', b''
    except OSError as e:
        return 'oserror', b'', str(e).encode()


def vectors(n, case_id):
    if n == 0:
        return [()]
    full = 9 ** n
    if full <= 6561:
        out = list(itertools.product(BOUNDARY, repeat=n))
    else:
        out = [tuple([v] * n) for v in BOUNDARY]
        for pos in range(n):
            for base in BOUNDARY:
                for v in BOUNDARY:
                    t = [base] * n
                    t[pos] = v
                    out.append(tuple(t))
        rs = random.Random('G1-boundary-sample-' + case_id)
        out += [tuple(rs.choice(BOUNDARY) for _ in range(n)) for _ in range(SAMPLE_CAP)]
    rr = random.Random('G1-random-' + case_id)
    out += [tuple(rr.getrandbits(64) for _ in range(n)) for _ in range(RANDOM_PER_CASE)]
    seen, uniq = set(), []
    for v in out:
        if v not in seen:
            seen.add(v)
            uniq.append(v)
    return uniq


def pack_vectors(vs, n):
    return struct.pack('<Q', len(vs)) + b''.join(struct.pack('<%dQ' % n, *v) for v in vs if n)


DRIVER = r'''#include <stdint.h>
#include <stdio.h>
%(proto)s
int main(void)
{
    uint64_t n, v[6] = {0};
    if (fread(&n, 8, 1, stdin) != 1)
        return 2;
    for (uint64_t j = 0; j < n; j++) {
        if (%(k)d && fread(v, 8, %(k)d, stdin) != %(k)d)
            return 2;
        %(call)s
    }
    return 0;
}
'''


def c_driver(name, n):
    proto = 'uint64_t %s(%s);' % (name, ', '.join(['uint64_t'] * n) or 'void')
    call = 'printf("0 %%llu\\n", (unsigned long long)%s(%s));' % (name, ', '.join('v[%d]' % i for i in range(n)))
    return DRIVER % {'proto': proto, 'k': n, 'call': call}


def native_driver(sym, n):
    proto = 'uint32_t %s(uint64_t *result%s);' % (sym, ''.join(', uint64_t' for _ in range(n)))
    call = ('uint64_t r = UINT64_C(6148914691236517205); uint32_t s = %s(&r%s); '
            'printf("%%u %%llu\\n", s, (unsigned long long)r);') % (sym, ''.join(', v[%d]' % i for i in range(n)))
    return DRIVER % {'proto': proto, 'k': n, 'call': call}


class Ctx:
    def __init__(self, a, work):
        self.a = a
        self.work = work
        self.cc = a.cc
        self.counter = 0

    def tmp(self, label):
        self.counter += 1
        d = self.work / ('%04d-%s' % (self.counter, re.sub(r'[^A-Za-z0-9_.-]', '_', label)))
        d.mkdir(parents=True)
        return d

    def tool(self, which, data, *args):
        return run([getattr(self.a, which)] + list(args), data)

    def compile_run(self, d, sources, n, vs, flags):
        for fname, text in sources:
            (d / fname).write_bytes(text if isinstance(text, bytes) else text.encode())
        exe = d / 'run'
        rc, _, err = run([self.cc] + flags + [str(d / f) for f, _ in sources] + ['-o', str(exe)], timeout=120)
        if rc != 0:
            return None, 'cc exit %s: %s' % (rc, err.decode(errors='replace')[:400])
        rc, out, err = run([exe], pack_vectors(vs, n), timeout=120)
        if rc != 0:
            return None, 'run exit %s' % rc
        rows = out.decode().split('\n')[:-1]
        if len(rows) != len(vs):
            return None, 'row count %d != %d' % (len(rows), len(vs))
        return [tuple(int(x) for x in r.split()) for r in rows], None

    def native(self, d, om_text, n, vs, sym='omega_calc'):
        """Pinned omc (function mode) -> pinned bridge generator -> caller linked with the binding."""
        om = om_text.encode()
        (d / 'unit.om').write_bytes(om)
        rc, binary, err = run([self.a.omc], om, timeout=120)
        if rc != 0 or not binary:
            return None, 'omc exit %s: %s' % (rc, err.decode(errors='replace')[:300]), None
        (d / 'unit.bin').write_bytes(binary)
        frame = om + b'\0' + b'%08d' % len(binary) + binary
        rc, binding, err = run([self.a.bridge_native], frame, timeout=120)
        if rc != 0 or not binding:
            return None, 'bridge generator exit %s: %s' % (rc, err.decode(errors='replace')[:300]), None
        res, why = self.compile_run(d, [('binding.c', binding), ('caller.c', native_driver(sym, n))], n, vs,
                                    ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror'])
        return res, why, {'unit_bin_sha256': sha(binary), 'binding_sha256': sha(binding)}


def compare(ref, got, vs, need_status=True):
    """First disagreement against the reference, as (vector, reference, got)."""
    for v, want, row in zip(vs, ref, got):
        st, val = row
        if (need_status and st != 0) or val != want:
            return {'vector': [str(x) for x in v], 'reference': str(want), 'got': str(val), 'status': st}
    return None


# ---------------------------------------------------------------- one unit through every representation

def check_unit(ctx, case_id, env, src, oracle_name, export_name, expect, ev):
    """env: the upper envelope (importer output, or validator-built semantic-only).
    expect: 'ok' or 'limit' (export must refuse 67). Returns the case record and the artifacts for mutants."""
    rec = {'id': case_id, 'reps': {}, 'checks': {}, 'failures': []}
    art = {}

    def fail(check, detail):
        rec['checks'][check] = 'FAIL'
        rec['failures'].append({'check': check, 'detail': detail})

    def ok(check):
        rec['checks'].setdefault(check, 'PASS')

    d = ctx.tmp(case_id)
    try:
        sem, side = decode_envelope(env)
        ops = decode_s1(sem)
        if src is not None:
            decode_sidecar(side, sem, len(ops), src)
    except Bad as e:
        rec['reps'] = {r: 'FAIL' if r == 'reference' else 'NOT_RUN' for r in REPS}
        fail('reference-decode', str(e))
        return rec, None
    n = arity(ops)
    ident = identity(sem)
    root = normalized_root(ops)
    vs = vectors(n, case_id)
    ref = [evaluate(ops, v) for v in vs]
    rec.update({'arity': n, 'records': len(ops), 'normalized': root is not None, 'semantic_identity': ident,
                'vectors': len(vs), 'expect_export': expect})
    rec['reps']['reference'] = 'PASS'
    art.update({'ops': ops, 'sem': sem, 'ident': ident, 'vs': vs, 'ref': ref, 'n': n, 'env': env, 'src': src,
                'export_name': export_name, 'identity_required': src is not None and root is not None})
    sem_only = envelope(sem)

    # 1. oracle
    if src is None:
        rec['reps']['oracle'] = 'NOT_APPLICABLE'
    else:
        (d / 'oracle').mkdir()
        res, why = ctx.compile_run(d / 'oracle', [('source.c', src), ('driver.c', c_driver(oracle_name, n))],
                                   n, vs, CC_FLAGS)
        if res is None:
            rec['reps']['oracle'] = 'FAIL'
            fail('oracle', why)
        else:
            m = compare(ref, res, vs)
            rec['reps']['oracle'] = 'FAIL' if m else 'PASS'
            if m:
                fail('oracle-vs-reference', m)
        ev['observations']['oracle'] += len(vs)

    # 3/4. export and re-import
    eargs = [export_name] if export_name != 'calc' else []
    rc, E, err = ctx.tool('export', env, *eargs)
    rc2, E2, _ = ctx.tool('export', env, *eargs)
    rc3, E3, _ = ctx.tool('export', sem_only, *eargs)
    rec['export_status'] = rc
    if (rc, E) != (rc2, E2):
        fail('export-deterministic', 'two runs differ')
    else:
        ok('export-deterministic')
    if (rc, E) != (rc3, E3):
        fail('export-generated-not-replayed', 'semantic-only envelope exports differently (status %s vs %s)' % (rc, rc3))
    else:
        ok('export-generated-not-replayed')
    expected_c = render_c(ops, export_name)
    bounds = rec['contract_rendering_bounds'] = c_bounds(ops, expected_c)
    over = bounds['bytes'] > 4096 or bounds['depth'] > 16 or bounds['reimport_ops'] > 64
    if over != (expect == 'limit'):
        fail('validator-case-design', 'expected %s but the contract rendering bounds are %r' % (expect, bounds))
    if expect == 'limit':
        rec['reps']['export'] = rec['reps']['reimport'] = 'NOT_APPLICABLE'
        if rc == 67 and E == b'':
            ok('export-limit-refusal')
        else:
            fail('export-limit-refusal', {'status': rc, 'stdout_bytes': len(E),
                                          'contract': 'section 6: unimportable export refuses 67 with no stdout'})
    elif rc != 0 or not E:
        rec['reps']['export'] = rec['reps']['reimport'] = 'FAIL'
        fail('export', {'status': rc, 'stderr': err.decode(errors='replace')[:300],
                        'contract': 'section 6: a required boundary that refuses is a FAIL'})
    else:
        (d / 'export.c').write_bytes(E)
        art['export'] = E.decode(errors='replace')
        if E.decode(errors='replace') == expected_c:
            ok('export-form')
        else:
            fail('export-form', {'expected': expected_c, 'got': E.decode(errors='replace')})
        rec['export_equals_source'] = None if src is None else E == src
        (d / 'export').mkdir()
        res, why = ctx.compile_run(d / 'export', [('export.c', E), ('driver.c', c_driver(export_name, n))], n, vs, CC_FLAGS)
        if res is None:
            rec['reps']['export'] = 'FAIL'
            fail('export-compile-run', why)
        else:
            m = compare(ref, res, vs)
            rec['reps']['export'] = 'FAIL' if m else 'PASS'
            if m:
                fail('export-vs-reference', m)
            ev['observations']['export'] += len(vs)
        rec['reps']['reimport'] = reimport_check(ctx, E, ops, sem, root, vs, ref, rec, fail, ok, export_name,
                                                 src is not None)
        if 'reimport_identity' in rec:
            ev['observations']['reimport'] += len(vs)

    # 5. native
    rc, L, err = ctx.tool('lower', env)
    rcb, L2, _ = ctx.tool('lower', env)
    rcc, L3, _ = ctx.tool('lower', sem_only)
    rec['lower_status'] = rc
    if (rc, L) != (rcb, L2):
        fail('lower-deterministic', 'two runs differ')
    else:
        ok('lower-deterministic')
    if (rc, L) != (rcc, L3):
        fail('lower-sidecar-independent', 'semantic-only envelope lowers differently')
    else:
        ok('lower-sidecar-independent')
    if rc != 0 or not L:
        rec['reps']['native'] = 'FAIL'
        fail('lower', {'status': rc, 'stderr': err.decode(errors='replace')[:300]})
    else:
        text = L.decode(errors='replace')
        art['lower'] = text
        expected_om = render_om(ops, 'calc', ident)
        if text == expected_om:
            ok('lower-form')
        else:
            fail('lower-form', {'expected': expected_om, 'got': text})
        m = re.match(r'^note omega\.universal\.lower\.v1 semantic ([0-9a-f]{64})\n', text)
        if m and m.group(1) == ident:
            ok('lower-semantic-note')
        else:
            fail('lower-semantic-note', 'note line does not carry the unit semantic identity %s' % ident)
        (d / 'native').mkdir()
        res, why, meta = ctx.native(d / 'native', text, n, vs)
        rec['native_artifacts'] = meta
        if res is None:
            rec['reps']['native'] = 'FAIL'
            fail('native', why)
        else:
            mm = compare(ref, res, vs)
            rec['reps']['native'] = 'FAIL' if mm else 'PASS'
            if mm:
                fail('native-vs-reference', mm)
            ev['observations']['native'] += len(vs)
            # determinism of the native realization: the same unit compiles to the same bytes
            rc4, b4, _ = run([ctx.a.omc], L, timeout=120)
            if rc4 == 0 and sha(b4) == meta['unit_bin_sha256']:
                ok('native-deterministic')
            else:
                fail('native-deterministic', 'omc output differs between runs')
    ev['observations']['reference'] += len(vs)
    rec['result'] = 'FAIL' if rec['failures'] else 'PASS'
    return rec, art


def reimport_check(ctx, E, ops, sem, root, vs, ref, rec, fail, ok, name, from_importer):
    """Section 4. A NORMALIZED unit from the C importer must re-import to the same identity. Any other unit
    is checked by equal evaluation; its identity difference is recorded, and the re-import must be
    NORMALIZED (the importer adds its pair). Every export must be a fixpoint: export(import(E)) == E."""
    rc, env2, err = ctx.tool('importer', E)
    if rc != 0 or not env2:
        fail('reimport', {'status': rc, 'stderr': err.decode(errors='replace')[:200]})
        return 'FAIL'
    try:
        sem2, side2 = decode_envelope(env2)
        ops2 = decode_s1(sem2)
        decode_sidecar(side2, sem2, len(ops2), E)
    except Bad as e:
        fail('reimport-decode', str(e))
        return 'FAIL'
    status = 'PASS'
    rec['reimport_identity'] = identity(sem2)
    if root is not None and from_importer:
        if sem2 != sem:
            fail('reimport-identity', {'original': identity(sem), 'reimported': identity(sem2),
                                       'contract': 'section 4: NORMALIZED U must re-import to the same identity'})
            status = 'FAIL'
        else:
            ok('reimport-identity')
    else:
        rec['reimport_identity_differs_recorded'] = sem2 != sem
        if normalized_root(ops2) is None:
            fail('reimport-identity', 'the re-import is not NORMALIZED')
            status = 'FAIL'
        else:
            ok('reimport-identity')
    for v, want in zip(vs, ref):
        got = evaluate(ops2, v)
        if got != want:
            fail('reimport-vs-reference', {'vector': [str(x) for x in v], 'reference': str(want), 'got': str(got)})
            status = 'FAIL'
            break
    rc, E2, _ = ctx.tool('export', env2, *([name] if name != 'calc' else []))
    if rc == 0 and E2 == E:
        ok('export-fixpoint')
    else:
        fail('export-fixpoint', 'export(import(export(U))) != export(U) (status %s)' % rc)
    return status


# ---------------------------------------------------------------- mutation controls

def mutant_checks(ctx, case_id, art, op_name, text, kind):
    """Run a mutated tool output through the comparisons the real output passed; list what detects it."""
    d = ctx.tmp('mut-%s-%s' % (case_id, op_name))
    ops, vs, ref, n = art['ops'], art['vs'], art['ref'], art['n']
    detected, notes = [], {}
    if kind == 'om':
        if text != render_om(ops, 'calc', art['ident']):
            detected.append('lower-form')
        res, why, _ = ctx.native(d, text, n, vs)
        if res is None:
            notes['native'] = why
            detected.append('native-build')
        else:
            m = compare(ref, res, vs)
            if m:
                detected.append('native-vs-reference')
                notes['first_mismatch'] = m
    else:
        name = art['export_name']
        if text != render_c(ops, name):
            detected.append('export-form')
        res, why = ctx.compile_run(d, [('export.c', text), ('driver.c', c_driver(name, n))], n, vs, CC_FLAGS)
        if res is None:
            notes['export'] = why
            detected.append('export-compile')
        else:
            m = compare(ref, res, vs)
            if m:
                detected.append('export-vs-reference')
                notes['first_mismatch'] = m
        rc, env2, _ = ctx.tool('importer', text.encode())
        if rc != 0:
            detected.append('reimport-refused')
        else:
            try:
                sem2, _ = decode_envelope(env2)
                ops2 = decode_s1(sem2)
                if art['identity_required'] and sem2 != art['sem']:
                    detected.append('reimport-identity')
                if any(evaluate(ops2, v) != w for v, w in zip(vs, ref)):
                    detected.append('reimport-vs-reference')
            except Bad:
                detected.append('reimport-decode')
    behaviour = [x for x in detected if x.endswith('vs-reference') or x == 'reimport-identity']
    return {'case': case_id, 'operator': op_name, 'mutant_sha256': sha(text.encode()), 'detected_by': detected,
            'killed': bool(detected), 'killed_by_behaviour_or_identity': bool(behaviour),
            **({'notes': notes} if notes else {})}


def run_mutants(ctx, arts, ev):
    rows = []
    for case_id, art in arts:
        if 'lower' in art:
            for op_name, fn in MUT.OM_OPERATORS:
                t = fn(art['lower'])
                if t is not None and t != art['lower']:
                    rows.append(mutant_checks(ctx, case_id, art, op_name, t, 'om'))
        if 'export' in art:
            ops = list(MUT.C_OPERATORS)
            if art['src'] is not None:
                ops.append(('c-replay-source', lambda _t, s=art['src']: s.decode(errors='replace')))
            for op_name, fn in ops:
                t = fn(art['export'])
                if t is not None and t != art['export']:
                    rows.append(mutant_checks(ctx, case_id, art, op_name, t, 'c'))
    by = {}
    for r in rows:
        b = by.setdefault(r['operator'], {'total': 0, 'killed': 0, 'killed_by_behaviour_or_identity': 0,
                                          'killed_by_form_only': 0, 'survivors': []})
        b['total'] += 1
        b['killed'] += r['killed']
        b['killed_by_behaviour_or_identity'] += r['killed_by_behaviour_or_identity']
        b['killed_by_form_only'] += r['killed'] and not r['killed_by_behaviour_or_identity']
        if not r['killed']:
            b['survivors'].append(r['case'])
    ev['mutants'] = {'total': len(rows), 'killed': sum(r['killed'] for r in rows),
                     'killed_by_behaviour_or_identity': sum(r['killed_by_behaviour_or_identity'] for r in rows),
                     'by_operator': by,
                     'rows': rows}


# ---------------------------------------------------------------- refusals

def refusal_checks(ctx, base_env, ev):
    rows = []

    def expect(label, which, data, args, want):
        rc, out, err = ctx.tool(which, data, *args)
        good = (rc == want) and out == b''
        rows.append({'tool': which, 'case': label, 'want': want, 'status': rc, 'stdout_bytes': len(out),
                     'stderr': err.decode(errors='replace')[:120], 'result': 'PASS' if good else 'FAIL'})

    sem, _ = decode_envelope(base_env)
    base_ops = decode_s1(sem)
    for label, bad in non_s1_envelopes(base_ops):
        e = envelope(bad)
        if ctx.a.sa:
            rc, out, _ = run([ctx.a.sa], b'\x02' + e)
            if rc != 0 or out != e:
                rows.append({'tool': 'sa', 'case': label, 'result': 'FAIL', 'status': rc,
                             'detail': 'probe is not a valid source-aware v1 unit; it cannot test 68'})
                continue
        for which in ('lower', 'export'):
            expect('non-S1 ' + label, which, e, [], 68)
    for label, bad in malformed_envelopes(base_env):
        if ctx.a.sa:
            rc, _, _ = run([ctx.a.sa], b'\x02' + bad)
            if rc != 65:
                rows.append({'tool': 'sa', 'case': label, 'result': 'FAIL', 'status': rc,
                             'detail': 'source-aware decoder does not call this MALFORMED'})
                continue
        for which in ('lower', 'export'):
            expect('malformed ' + label, which, bad, [], 65)
    for nm in ['9x', 'a-b', 'x' * 64, 'end', '']:
        expect('bad name %r' % nm[:12], 'lower', base_env, [nm], 64)
    expect('two arguments', 'lower', base_env, ['calc', 'calc'], 64)
    for nm in ['foo', 'Calc', 'omega_', 'main', 'omega_' + 'x' * 27, 'a b', '']:
        expect('bad name %r' % nm[:12], 'export', base_env, [nm], 64)
    expect('two arguments', 'export', base_env, ['calc', 'calc'], 64)
    ev['tool_refusals'] = {'total': len(rows), 'pass': sum(r['result'] == 'PASS' for r in rows), 'rows': rows}


def named_checks(ctx, env, ev):
    """`lower NAME` binds omega_NAME; `c-export omega_x` names the C function."""
    rows = []
    sem, _ = decode_envelope(env)
    ops = decode_s1(sem)
    n = arity(ops)
    vs = vectors(n, 'named')
    ref = [evaluate(ops, v) for v in vs]
    rc, L, _ = ctx.tool('lower', env, 'fold')
    if rc == 0 and L.decode(errors='replace') == render_om(ops, 'fold', identity(sem)):
        res, why, _ = ctx.native(ctx.tmp('named-lower'), L.decode(), n, vs, 'omega_fold')
        rows.append({'case': 'lower fold -> omega_fold', 'result': 'PASS' if res and not compare(ref, res, vs) else 'FAIL',
                     **({'detail': why} if why else {})})
    else:
        rows.append({'case': 'lower fold -> omega_fold', 'result': 'FAIL', 'status': rc})
    rc, E, _ = ctx.tool('export', env, 'omega_x1')
    rows.append({'case': 'c-export omega_x1', 'status': rc,
                 'result': 'PASS' if rc == 0 and E.decode(errors='replace') == render_c(ops, 'omega_x1') else 'FAIL'})
    ev['named'] = rows


# ---------------------------------------------------------------- pins and inputs

def check_pins(a):
    pins = {}
    p = ROOT / CONTRACT[0]
    if not p.is_file() or sha(p.read_bytes()) != CONTRACT[1]:
        raise NotRun('contract %s sha256 is not %s' % CONTRACT)
    pins[CONTRACT[0]] = CONTRACT[1]
    for rel, want in FROZEN.items():
        got = sha((ROOT / rel).read_bytes()) if (ROOT / rel).is_file() else None
        if got != want:
            raise NotRun('frozen input %s sha256 %s, contract/validator pin %s' % (rel, got, want))
        pins[rel] = got
    tools = {}
    for key in ('lower', 'export', 'importer', 'omc', 'bridge_native'):
        path = getattr(a, key)
        if not path or not Path(path).is_file() or not os.access(path, os.X_OK):
            raise NotRun('tool %s absent or not executable: %s' % (key, path))
        tools[key] = {'path': str(path), 'sha256': sha(Path(path).read_bytes())}
    if tools['omc']['sha256'] != OMC_SHA:
        raise NotRun('omc sha256 %s, contract section 1 pins %s' % (tools['omc']['sha256'], OMC_SHA))
    imp = Path(a.importer)
    try:
        m = json.loads((imp.parent / 'build.json').read_text())
    except (OSError, ValueError) as e:
        raise NotRun('importer build.json unreadable: %s' % e)
    if m.get('compiler_sha256') != IMPORT_COMPILER_SHA or m.get('binary_sha256') != tools['importer']['sha256']:
        raise NotRun('importer does not match its build.json / compiler pin %s' % IMPORT_COMPILER_SHA)
    if set(m.get('inputs', {})) != set(IMPORT_INPUTS) or any(sha((ROOT / r).read_bytes()) != m['inputs'][r] for r in IMPORT_INPUTS):
        raise NotRun('importer build.json inputs differ from this checkout')
    tools['importer']['build_json'] = m
    repo = Path(a.bridge_repo)
    rc, src, _ = run(['git', '-C', repo, 'show', BRIDGE_COMMIT + ':src/native.om'])
    if rc != 0:
        raise NotRun('bridge repository %s lacks pinned commit %s' % (repo, BRIDGE_COMMIT))
    rc, built, _ = run([a.omc], src, timeout=120)
    if rc != 0 or built != Path(a.bridge_native).read_bytes():
        raise NotRun('bridge generator is not omc(native.om @ %s)' % BRIDGE_COMMIT)
    tools['bridge_native'].update({'commit': BRIDGE_COMMIT, 'native_om_sha256': sha(src)})
    for key in ('lower', 'export'):
        top = run(['git', '-C', Path(getattr(a, key)).resolve().parent, 'rev-parse', 'HEAD'])
        dirty = run(['git', '-C', Path(getattr(a, key)).resolve().parent, 'status', '--porcelain', '--untracked-files=no'])
        tools[key]['repo_head'] = top[1].decode().strip() if top[0] == 0 else None
        tools[key]['repo_dirty'] = bool(dirty[1].strip()) if dirty[0] == 0 else None
        last = run(['git', '-C', Path(getattr(a, key)).resolve().parent, 'log', '-1', '--format=%H', '--',
                    Path(getattr(a, key)).resolve()])
        tools[key]['last_commit_touching_command'] = last[1].decode().strip() if last[0] == 0 else None
        repo_root = Path(getattr(a, key)).resolve().parents[2]
        exe_name = Path(getattr(a, key)).name
        for bj in sorted(repo_root.glob('build/*/build.json')):
            if (bj.parent / exe_name).is_file():
                tools[key]['build'] = {'dir': str(bj.parent.relative_to(repo_root)), 'build_json_sha256': sha(bj.read_bytes()),
                                       'binary_sha256': sha((bj.parent / exe_name).read_bytes()),
                                       'build_json': json.loads(bj.read_text())}
    if a.sa:
        tools['sa'] = {'path': str(a.sa), 'sha256': sha(Path(a.sa).read_bytes())}
    return pins, tools


def load_fixtures():
    d = json.loads((ROOT / 'tests/universal/translation/fixtures-f1.json').read_text())
    pos = []
    for p in d['positives']:
        b = p['source'].encode()
        if sha(b) != p['sha256']:
            raise NotRun('fixture %s bytes do not match its sha256' % p['id'])
        pos.append((p['id'], b, p['name'], p['inputs']))
    spec = importlib.util.spec_from_file_location('f1gate', ROOT / 'tests/universal/c_import_gate.py')
    g = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(g)  # data only: NEGATIVES; its decode/evaluate helpers are not used
    negs = list(g.NEGATIVES)
    if len(negs) != len(d['refusals']) or any(sha(b) != r['sha256'] or len(b) != r['bytes'] for b, r in zip(negs, d['refusals'])):
        raise NotRun('F1 refusal bytes do not match fixtures-f1.json')
    return pos, [(r['id'], b) for r, b in zip(d['refusals'], negs)]


def controls():
    """The four accepted boundaries of c_import_controls.py (pinned above by sha256), rebuilt byte for byte."""
    header = '#include <stdint.h>\n'
    base = (header + 'uint64_t calc(uint64_t a,uint64_t b){return a+b;}').encode()
    pad = base + b' ' * (4096 - len(base))
    comment = base + b'//' + b'x' * (4096 - len(base) - 2)
    maximum = (header + 'uint64_t calc(uint64_t a,uint64_t b){return ' + '+'.join(['a'] * 61) + ';}').encode()
    deep = (header + 'uint64_t calc(uint64_t a,uint64_t b){return ' + '(' * 16 + 'a' + ')' * 16 + ';}').encode()
    return [('C-exact-source-capacity', pad), ('C-comment-at-capacity', comment),
            ('C-operation-capacity', maximum), ('C-nesting-limit', deep)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for k in ('lower', 'export', 'importer', 'omc', 'bridge_native'):
        ap.add_argument('--' + k.replace('_', '-'), dest=k)
    ap.add_argument('--bridge-repo', default=os.path.expanduser('~/workspace/omega-bridge'),
                    help='omega-bridge clone holding commit %s (the generator is rebuilt and compared)' % BRIDGE_COMMIT)
    ap.add_argument('--sa', help='optional #621 native sa binary: confirms refusal probes are valid / malformed v1')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--no-mutants', action='store_true')
    ap.add_argument('--out', required=True, help='new directory; evidence.json is written inside it')
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    work = out / 'work'
    work.mkdir()
    ev = {'schema': 'omega.universal.translation.validate.g1.v1', 'contract': {'path': CONTRACT[0], 'sha256': CONTRACT[1]},
          'validator_sha256': sha(Path(__file__).read_bytes()),
          'mutant_operators_sha256': sha((HERE / 'mutants/operators.py').read_bytes()),
          'evidence_class': 'REFERENCE + DIFFERENTIAL, NATIVE (host AArch64); no PROOF',
          'boundary_values': [str(x) for x in BOUNDARY], 'random_per_case': RANDOM_PER_CASE,
          'observations': {r: 0 for r in REPS}}
    try:
        ev['pins'], ev['tools'] = check_pins(a)
        positives, negatives = load_fixtures()
    except NotRun as e:
        ev['result'] = 'NOT_RUN'
        ev['reason'] = str(e)
        (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
        print('NOT_RUN', e)
        return 3
    ev['cc_version'] = run([a.cc, '--version'])[1].decode().split('\n')[0]
    ctx = Ctx(a, work)
    cases, arts = [], []

    def import_case(cid, src, name):
        rc, env, err = run([a.importer], src)
        if rc != 0:
            rec = {'id': cid, 'reps': {r: 'NOT_RUN' for r in REPS}, 'result': 'FAIL',
                   'failures': [{'check': 'import', 'detail': {'status': rc, 'stderr': err.decode(errors='replace')}}]}
            cases.append(rec)
            return None
        rec, art = check_unit(ctx, cid, env, src, name, name, 'ok', ev)
        rec['kind'] = 'source'
        rec['source_sha256'] = sha(src)
        cases.append(rec)
        if art:
            arts.append((cid, art))
        return env

    envs = {}
    for cid, src, name, k in positives:
        envs[cid] = import_case(cid, src, name)
        print(cid, cases[-1]['result'], flush=True)
    for cid, src in controls():
        envs[cid] = import_case(cid, src, 'calc')
        print(cid, cases[-1]['result'], flush=True)
    for cid, ops, expect in probes():
        rec, art = check_unit(ctx, cid, envelope(build_sem(ops)), None, None, 'calc', expect, ev)
        rec['kind'] = 'probe'
        cases.append(rec)
        if art and expect == 'ok':
            arts.append((cid, art))
        print(cid, rec['result'], flush=True)

    # generated, not replayed: comments + CRLF + renamed parameters (F1-P33) export to different text with
    # equal semantics, and a semantic-only envelope exports the same bytes (checked for every case above)
    p33 = next(c for c in cases if c['id'] == 'F1-P33')
    g = {'export_differs_from_source': p33.get('export_equals_source') is False,
         'reimport_identity_equal': p33['checks'].get('reimport-identity') == 'PASS',
         'semantic_only_export_identical': p33['checks'].get('export-generated-not-replayed') == 'PASS'}
    g['result'] = 'PASS' if all(g.values()) else 'FAIL'
    ev['generated_not_replayed_P33'] = g
    # import refusals F1
    rrows = []
    for rid, src in negatives:
        rc, o, err = run([a.importer], src)
        rrows.append({'id': rid, 'status': rc, 'stdout_bytes': len(o), 'result': 'PASS' if rc not in (0, 'timeout', 'oserror') and o == b'' and b'C_IMPORT_' in err else 'FAIL'})
    ev['f1_refusals'] = {'total': len(rrows), 'pass': sum(r['result'] == 'PASS' for r in rrows), 'rows': rrows}
    base = envs.get('F1-P05')
    if base:
        refusal_checks(ctx, base, ev)
        if envs.get('F1-P34'):
            named_checks(ctx, envs['F1-P34'], ev)
    if not a.no_mutants:
        run_mutants(ctx, arts, ev)

    summary = {r: {'PASS': 0, 'FAIL': 0, 'NOT_RUN': 0, 'NOT_APPLICABLE': 0} for r in REPS}
    for c in cases:
        for r in REPS:
            summary[r][c['reps'].get(r, 'NOT_RUN')] += 1
    ev['representations'] = summary
    ev['cases'] = cases
    failed = [c['id'] for c in cases if c['result'] != 'PASS']
    failed += ['refusal:' + r['id'] for r in ev['f1_refusals']['rows'] if r['result'] != 'PASS']
    failed += ['tool-refusal:%s:%s' % (r['tool'], r['case']) for r in ev.get('tool_refusals', {}).get('rows', []) if r['result'] != 'PASS']
    failed += ['named:' + r['case'] for r in ev.get('named', []) if r['result'] != 'PASS']
    if ev['generated_not_replayed_P33']['result'] != 'PASS':
        failed.append('generated-not-replayed:F1-P33')
    if not base:
        failed.append('refusal probes NOT_RUN: F1-P05 did not import')
    if 'mutants' in ev and ev['mutants']['killed'] != ev['mutants']['total']:
        failed.append('mutants survived: %d' % (ev['mutants']['total'] - ev['mutants']['killed']))
    ev['failed'] = failed
    ev['result'] = 'FAIL' if failed else 'PASS'
    (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
    print(json.dumps({'result': ev['result'], 'representations': summary,
                      'f1_refusals': '%d/%d' % (ev['f1_refusals']['pass'], ev['f1_refusals']['total']),
                      'tool_refusals': '%d/%d' % (ev.get('tool_refusals', {}).get('pass', 0), ev.get('tool_refusals', {}).get('total', 0)),
                      'mutants': '%d/%d killed (%d by behaviour or identity, rest by contract form only)' % (
                          ev['mutants']['killed'], ev['mutants']['total'], ev['mutants']['killed_by_behaviour_or_identity'])
                      if 'mutants' in ev else 'NOT_RUN',
                      'failed': failed[:40]}, indent=1))
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
