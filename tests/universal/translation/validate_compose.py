#!/usr/bin/env python3
"""Independent G2 composition validation (#623) against spec/universal/translation-compose-v1.md.

For the S1 family members C, Rust and Python, every directed composition A -> B (A != B, 6 in G2) and
every fixture of A (F1 + accepted C import controls for C, fixtures-f2-<lang>.json for Rust and Python):
  U_A = import_A(src); src_B = export_B(U_A); U_B = import_B(src_B)
  4.1 identity    N(strip(U_B)) == N(strip(U_A)) byte for byte
  4.2 DIFFERENTIAL A's oracle on src, this file's reference evaluator on U_A, B's oracle on src_B and the
                  native realization of U_A (R to C -> lower -> pinned omc -> pinned bridge -> omega_calc)
                  agree on the translation-v1 section 3 vectors
  4.3 generated   src_B equals export_B of the semantic-only envelope (and of R(U_A, M) for every member M)
Same-language round trips A -> A are reported separately. R, N, strip, the decoders, the renderings of
each exporter's frozen output form and the mutation operators are written here from the contracts; the
adapters are exercised only through their public commands, their sources are not read.
Oracles: cc (C17), rustc --edition 2021 -O -C overflow-checks=on (Rust), python3 (Python).
Exit: 0 PASS, 1 FAIL, 3 NOT_RUN (a pin or tool is absent; nothing is passed).
"""
from pathlib import Path
from types import SimpleNamespace
import argparse, hashlib, json, os, re, struct, sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import validate_g1 as G  # noqa: E402  (this lane's own G1 validator: decoders, vectors, native seam)

ROOT = G.ROOT
MASK = G.MASK
U = 65535
COMPOSE = ('spec/universal/translation-compose-v1.md', '51a7cc444e50fd114a6d2e2272d645333cec1ea92603a60cd9dde06fc7d4c3a0')
LANE_SPECS = {'rust': 'spec/universal/rust-u64-v1.md', 'python': 'spec/universal/python-u64-v1.md'}
# compose section 1: header language, profile, op dialect byte, arithmetic (ops 2/3/4) obligations
MEMBERS = {'c': (1, 1, 1, 1), 'rust': (2, 2, 2, 1), 'python': (3, 4, 3, 5)}
LANGS = ['c', 'rust', 'python']
# compose section 3: each importer's declared normalization pair (c-u64-import-v1, rust-u64-v1 section 3,
# python-u64-v1 section 3 all declare one)
DECLARES_PAIR = {'c': True, 'rust': True, 'python': True}
PY_MASK = '0xFFFFFFFFFFFFFFFF'
TOOLS = {'c': ('c-import', 'c-export'), 'rust': ('rust-import', 'rust-export'),
         'python': ('python-import', 'python-export')}
RUSTC = ['--edition', '2021', '-O', '-C', 'overflow-checks=on']


class Bad(Exception):
    pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------- family decoder, R, N, strip

def decode_family(sem):
    """source-aware-v1 semantic bytes of an S1-family unit -> (member, ops)."""
    if len(sem) < 44 or sem[:4] != b'OMSA' or sem[4] != 1 or sem[7] != 1:
        raise Bad('semantic header')
    count = struct.unpack_from('<H', sem, 10)[0]
    if not 1 <= count <= 64 or len(sem) != 44 + 20 * count:
        raise Bad('operation count')
    if sem[12:44] != G.SEMANTICS_DIGEST:
        raise Bad('semantics digest')
    member = next((m for m, v in MEMBERS.items() if (sem[5], sem[6]) == v[:2]), None)
    if member is None or (sem[8], sem[9]) != (0, 1):
        raise Bad('not an S1-family header %r' % (tuple(sem[5:10]),))
    _, _, dialect, arith = MEMBERS[member]
    ops, slots = [], []
    for k in range(count):
        d, v, op, t, attr, eff, alias, obl, a, b, imm = struct.unpack_from('<8BHHQ', sem, 44 + 20 * k)
        if (d, v, t, eff, alias) != (dialect, 1, 1, 0, 0):
            raise Bad('record %d fields' % k)
        if op == 9:
            if (attr, obl, a, b) != (0, 1, U, U) or imm > 5:
                raise Bad('record %d input' % k)
            slots.append(imm)
        elif op == 1:
            if (attr, obl, a, b) != (1, 0, U, U):
                raise Bad('record %d constant' % k)
        elif op in (2, 3, 4):
            if (attr, obl, imm) != (1, arith, 0) or a >= k or b >= k:
                raise Bad('record %d arithmetic' % k)
        else:
            raise Bad('record %d opcode %d' % (k, op))
        ops.append((op, a, b, imm))
    if sorted(slots) != list(range(len(slots))):
        raise Bad('input slots not a permutation')
    return member, ops


def R(sem, member):
    """compose section 2: rewrite header language/profile, every dialect byte and arithmetic obligations."""
    lang, prof, dialect, arith = MEMBERS[member]
    b = bytearray(sem)
    b[5], b[6] = lang, prof
    for k in range(struct.unpack_from('<H', sem, 10)[0]):
        o = 44 + 20 * k
        b[o] = dialect
        if b[o + 2] in (2, 3, 4):
            b[o + 7] = arith
    return bytes(b)


def N(sem):
    return R(sem, 'c')


def strip(sem, ops, member):
    """compose section 3: remove one trailing NORMALIZED pair iff the producing importer declares one."""
    if not DECLARES_PAIR[member] or G.normalized_root(ops) is None:
        return sem
    c = len(ops) - 2
    return sem[:10] + struct.pack('<H', c) + sem[12:44 + 20 * c]


# ---------------------------------------------------------------- expression trees and frozen output forms

def tree(ops, k):
    op, a, b, imm = ops[k]
    if op == 9:
        return {'t': 'in', 's': imm}
    if op == 1:
        return {'t': 'k', 'v': imm}
    return {'t': 'op', 'op': {2: '+', 3: '-', 4: '*'}[op], 'l': tree(ops, a), 'r': tree(ops, b),
            'mask': PY_MASK, 'plain': False}


def root_tree(ops):
    r = G.normalized_root(ops)
    return tree(ops, len(ops) - 1 if r is None else r)


RUST_M = {'+': 'add', '-': 'sub', '*': 'mul'}


def rust_expr(t):
    if t['t'] == 'in':
        return 'i%d' % t['s']
    if t['t'] == 'k':
        return '%du64' % t['v']
    if t['plain']:
        return '(%s %s %s)' % (rust_expr(t['l']), t['op'], rust_expr(t['r']))
    return '%s.wrapping_%s(%s)' % (rust_expr(t['l']), RUST_M[t['op']], rust_expr(t['r']))


def py_expr(t, top):
    if t['t'] == 'in':
        return 'i%d' % t['s']
    if t['t'] == 'k':
        return '%d' % t['v']
    e = '(%s %s %s)' % (py_expr(t['l'], False), t['op'], py_expr(t['r'], False))
    if t['mask'] is not None:
        e += ' & ' + t['mask']
        if not top:
            e = '(' + e + ')'
    return e


def c_expr(t):
    if t['t'] == 'in':
        return 'i%d' % t['s'], 3
    if t['t'] == 'k':
        return '%dULL' % t['v'], 3
    p = 2 if t['op'] == '*' else 1
    (lt, lp), (rt, rp) = c_expr(t['l']), c_expr(t['r'])
    if lp < p:
        lt = '(' + lt + ')'
    if rp <= p:
        rt = '(' + rt + ')'
    return '%s %s %s' % (lt, t['op'], rt), p


def render(lang, t, n):
    if lang == 'c':
        params = ', '.join('uint64_t i%d' % s for s in range(n)) or 'void'
        return '#include <stdint.h>\nuint64_t calc(%s) { return %s; }\n' % (params, c_expr(t)[0])
    if lang == 'rust':
        return 'pub fn calc(%s) -> u64 { %s }\n' % (', '.join('i%d: u64' % s for s in range(n)), rust_expr(t))
    return 'def calc(%s):\n    return %s\n' % (', '.join('i%d' % s for s in range(n)), py_expr(t, True))


def bounds(lang, t, n):
    """Re-import bounds of each exporter's contract rendering (translation-v1 section 6, rust-u64-v1
    section 4, python-u64-v1 section 6): True iff the export must refuse 67 LIMIT."""
    def nodes(x):
        return 0 if x['t'] == 'in' else 1 if x['t'] == 'k' else 1 + nodes(x['l']) + nodes(x['r'])

    def rdepth(x):
        return 0 if x['t'] != 'op' else max(rdepth(x['l']), rdepth(x['r']) + 1)

    text = render(lang, t, n)
    ops = n + nodes(t) + 2
    body = text.split('return ' if lang != 'rust' else '-> u64 { ', 1)[1]
    depth = best = 0
    for ch in body:
        depth += {'(': 1, ')': -1}.get(ch, 0)
        best = max(best, depth)
    if lang == 'c':
        over = best > 16
    elif lang == 'rust':
        over = rdepth(t) > 16
    else:
        over = best > 128
    return {'bytes': len(text.encode()), 'reimport_ops': ops, 'depth': rdepth(t) if lang == 'rust' else best,
            'over': len(text.encode()) > 4096 or ops > 64 or over}


# ---------------------------------------------------------------- parsers of the exporters' output forms

class P:
    def __init__(self, s):
        self.s, self.i = s, 0

    def eat(self, lit):
        if not self.s.startswith(lit, self.i):
            raise Bad('expected %r at %d' % (lit, self.i))
        self.i += len(lit)

    def peek(self, lit):
        return self.s.startswith(lit, self.i)

    def num(self):
        m = re.compile(r'\d+').match(self.s, self.i)
        if not m:
            raise Bad('number at %d' % self.i)
        self.i = m.end()
        return int(m.group())


def parse_rust(text):
    m = re.match(r'^pub fn calc\(([^)]*)\) -> u64 \{ (.*) \}\n$', text, re.S)
    if not m:
        raise Bad('rust frame')
    p = P(m.group(2))

    def primary():
        if p.peek('i'):
            p.eat('i')
            return {'t': 'in', 's': p.num()}
        v = p.num()
        p.eat('u64')
        return {'t': 'k', 'v': v}

    def expr():
        x = primary()
        while p.peek('.wrapping_'):
            p.eat('.wrapping_')
            op = next(o for o, w in RUST_M.items() if p.peek(w))
            p.eat(RUST_M[op] + '(')
            y = expr()
            p.eat(')')
            x = {'t': 'op', 'op': op, 'l': x, 'r': y, 'mask': PY_MASK, 'plain': False}
        return x

    t = expr()
    if p.i != len(p.s):
        raise Bad('rust trailing text')
    return t


def parse_python(text):
    m = re.match(r'^def calc\(([^)]*)\):\n    return (.*)\n$', text, re.S)
    if not m:
        raise Bad('python frame')
    p = P(m.group(2))

    def operand():
        if p.peek('i'):
            p.eat('i')
            return {'t': 'in', 's': p.num()}
        if p.peek('('):
            p.eat('(')
            x = masked()
            p.eat(')')
            return x
        return {'t': 'k', 'v': p.num()}

    def masked():
        p.eat('(')
        a = operand()
        p.eat(' ')
        op = p.s[p.i]
        p.eat(op + ' ')
        b = operand()
        p.eat(') & ' + PY_MASK)
        return {'t': 'op', 'op': op, 'l': a, 'r': b, 'mask': PY_MASK, 'plain': False}

    t = masked() if p.peek('(') else operand()
    if p.i != len(p.s):
        raise Bad('python trailing text')
    return t


def clone(t):
    return json.loads(json.dumps(t))


def preorder(t):
    out = [t]
    if t['t'] == 'op':
        out += preorder(t['l']) + preorder(t['r'])
    return out


def mut_op_swap(t):
    x = next((y for y in preorder(t) if y['t'] == 'op'), None)
    if x:
        x['op'] = {'+': '-', '-': '+', '*': '+'}[x['op']]
        return t


def mut_operand_swap(t):
    x = next((y for y in preorder(t) if y['t'] == 'op' and y['l'] != y['r']), None)
    if x:
        x['l'], x['r'] = x['r'], x['l']
        return t


def mut_const(t):
    x = next((y for y in preorder(t) if y['t'] == 'k'), None)
    if x:
        x['v'] = (x['v'] + 1) & MASK
        return t


def mut_slot_swap(t):
    hit = False
    for y in preorder(t):
        if y['t'] == 'in' and y['s'] in (0, 1):
            y['s'] = 1 - y['s']
            hit = True
    return t if hit else None


def mut_plus0_dup(t):
    return {'t': 'op', 'op': '+', 'l': t, 'r': {'t': 'k', 'v': 0}, 'mask': PY_MASK, 'plain': False}


def mut_plus0_strip(t):
    if t['t'] == 'op' and t['op'] == '+' and t['r'] == {'t': 'k', 'v': 0}:
        return t['l']


def mut_plain(t):
    x = next((y for y in preorder(t) if y['t'] == 'op'), None)
    if x:
        x['plain'] = True
        return t


def mut_mask_drop(t):
    x = next((y for y in preorder(t) if y['t'] == 'op'), None)
    if x:
        x['mask'] = None
        return t


def mut_mask_wrong(t):
    x = next((y for y in preorder(t) if y['t'] == 'op'), None)
    if x:
        x['mask'] = '0xFFFFFFFFFFFFFFFE'
        return t


COMMON = [('op-swap', mut_op_swap), ('operand-swap', mut_operand_swap), ('const-off-by-one', mut_const),
          ('slot-swap', mut_slot_swap), ('plus0-dup', mut_plus0_dup), ('plus0-strip', mut_plus0_strip)]
TREE_MUTANTS = {'rust': COMMON + [('plain-operator', mut_plain)],
                'python': COMMON + [('mask-drop', mut_mask_drop), ('mask-wrong', mut_mask_wrong)]}


def tree_text(lang, t, n):
    if lang == 'rust':
        return 'pub fn calc(%s) -> u64 { %s }\n' % (', '.join('i%d: u64' % s for s in range(n)), rust_expr(t))
    return 'def calc(%s):\n    return %s\n' % (', '.join('i%d' % s for s in range(n)), py_expr(t, True))


# ---------------------------------------------------------------- oracles

RUST_DRIVER = '''
#[allow(unused_variables)]
fn main() {
    use std::io::{Read, Write};
    let mut b = Vec::new();
    std::io::stdin().read_to_end(&mut b).unwrap();
    let rd = |o: usize| u64::from_le_bytes(b[o..o + 8].try_into().unwrap());
    let n = rd(0) as usize;
    let mut out = String::new();
    for j in 0..n {
        let base = 8 + 8 * %(k)d * j;
        out.push_str(&format!("0 {}\\n", %(name)s(%(args)s)));
    }
    std::io::stdout().write_all(out.as_bytes()).unwrap();
}
'''
PY_DRIVER = '''import struct, sys
path, name, k = sys.argv[1], sys.argv[2], int(sys.argv[3])
ns = {}
exec(compile(open(path, 'rb').read(), path, 'exec'), ns)
f = ns[name]
b = sys.stdin.buffer.read()
n = struct.unpack_from('<Q', b, 0)[0]
out = []
for j in range(n):
    v = struct.unpack_from('<%dQ' % k, b, 8 + 8 * k * j) if k else ()
    r = f(*v)
    ok = type(r) is int and 0 <= r <= 0xFFFFFFFFFFFFFFFF
    out.append('%d %d' % (0 if ok else 1, r if type(r) is int else -1))
sys.stdout.write('\\n'.join(out) + ('\\n' if out else ''))
'''


class Run:
    def __init__(self, a, out):
        self.a = a
        self.ctx = G.Ctx(a, out / 'work')
        self.cache = {}

    def tool(self, name, data, *args):
        path = self.a.tools.get(name)
        if path is None:
            return 'absent', b'', b''
        return G.run([path] + list(args), data)

    def oracle(self, lang, src, name, n, vs, key):
        k = ('oracle', lang, sha(src), name, n, key)
        if k in self.cache:
            return self.cache[k]
        d = self.ctx.tmp('oracle-%s-%s' % (lang, key))
        if lang == 'c':
            res = self.ctx.compile_run(d, [('source.c', src), ('driver.c', G.c_driver(name, n))], n, vs, G.CC_FLAGS)
        elif lang == 'rust':
            prog = src + b'\n' + (RUST_DRIVER % {'k': n, 'name': name,
                                                 'args': ', '.join('rd(base + %d)' % (8 * i) for i in range(n))}).encode()
            (d / 'prog.rs').write_bytes(prog)
            rc, _, err = G.run([self.a.rustc] + RUSTC + ['-o', d / 'run', d / 'prog.rs'], timeout=180)
            if rc != 0:
                res = (None, 'rustc exit %s: %s' % (rc, err.decode(errors='replace')[:300]))
            else:
                res = self.collect(G.run([d / 'run'], G.pack_vectors(vs, n), timeout=120), vs)
        else:
            (d / 'prog.py').write_bytes(src)
            (d / 'driver.py').write_text(PY_DRIVER)
            res = self.collect(G.run([self.a.python, '-I', d / 'driver.py', d / 'prog.py', name, str(n)],
                                     G.pack_vectors(vs, n), timeout=120), vs)
        self.cache[k] = res
        return res

    @staticmethod
    def collect(r, vs):
        rc, out, err = r
        if rc != 0:
            return None, 'run exit %s: %s' % (rc, err.decode(errors='replace')[-200:])
        rows = out.decode().split('\n')[:-1]
        if len(rows) != len(vs):
            return None, 'row count %d != %d' % (len(rows), len(vs))
        return [tuple(int(x) for x in r.split()) for r in rows], None

    def native(self, csem, n, vs, key):
        """translation-v1 section 5 on R(U, C): lower -> pinned omc -> pinned bridge -> omega_calc."""
        k = ('native', sha(csem), key)
        if k in self.cache:
            return self.cache[k]
        rc, L, err = self.tool('lower', G.envelope(csem))
        if rc != 0:
            res = (None, 'lower exit %s: %s' % (rc, err.decode(errors='replace')[:200]), None)
        else:
            _, ops = decode_family(csem)
            form = L.decode(errors='replace') == G.render_om(ops, 'calc', G.identity(csem))
            r = self.ctx.native(self.ctx.tmp('native-%s' % key), L.decode(errors='replace'), n, vs)
            res = (r[0], r[1] if form else 'lower output differs from translation-v1 section 5 form', r[2])
            if not form:
                res = (None, res[1], None)
        self.cache[k] = res
        return res

    def imp(self, lang, src):
        k = ('import', lang, sha(src))
        if k not in self.cache:
            self.cache[k] = self.tool(TOOLS[lang][0], src)
        return self.cache[k]


# ---------------------------------------------------------------- fixtures

def load_fixtures():
    """(lang, id, base id for vectors, source bytes, function name, inputs); NOT_IN_PROFILE records."""
    out, nip, info = [], {}, {}
    f1 = json.loads((ROOT / 'tests/universal/translation/fixtures-f1.json').read_text())
    f1ids = {p['id']: p['sha256'] for p in f1['positives']}
    for p in f1['positives']:
        b = p['source'].encode()
        if sha(b) != p['sha256']:
            raise Bad('F1 %s sha256' % p['id'])
        out.append(('c', p['id'], p['id'], b, p['name'], p['inputs']))
    for cid, src in G.controls():
        out.append(('c', cid, cid, src, 'calc', 2))
    for lang in ('rust', 'python'):
        path = ROOT / ('tests/universal/translation/fixtures-f2-%s.json' % lang)
        if not path.is_file():
            info[lang] = {'fixtures': 'absent'}
            continue
        d = json.loads(path.read_text())
        info[lang] = {'path': str(path.relative_to(ROOT)), 'sha256': sha(path.read_bytes()),
                      'positives': len(d['positives']), 'controls': len(d.get('controls', [])),
                      'refusals': len(d.get('refusals', []))}
        for p in d['positives']:
            b = p['source'].encode()
            f1id = p.get('f1_id') or p.get('f1')
            if sha(b) != p['sha256'] or f1ids.get(f1id) != p['f1_sha256'] or p['id'] != 'F2-%s-%s' % (lang, f1id):
                raise Bad('%s fixture %s: sha256 or F1 link' % (lang, p['id']))
            out.append((lang, p['id'], f1id, b, p['name'], p['inputs']))
        for c in d.get('controls', []):
            b = c['source'].encode()
            if sha(b) != c['sha256']:
                raise Bad('%s control %s sha256' % (lang, c['id']))
            out.append((lang, c['id'], 'C-' + c['control'], b, c['name'], c['inputs']))
        nip[lang] = d.get('not_in_profile', [])
    return out, nip, info


def lane_refusals(lang):
    path = ROOT / ('tests/universal/translation/fixtures-f2-%s.json' % lang)
    if not path.is_file():
        return []
    rows = []
    for r in json.loads(path.read_text()).get('refusals', []):
        b = bytes.fromhex(r['source_hex']) if 'source_hex' in r else r['source'].encode()
        rows.append((r['id'], b, r['status'], sha(b) == r['sha256']))
    return rows


# ---------------------------------------------------------------- one composition

def compose(run, ev, fx, B, U_A):
    lang, fid, base, src, name, n = fx
    env, sem, ops, vs, ref = U_A['env'], U_A['sem'], U_A['ops'], U_A['vs'], U_A['ref']
    rec = {'fixture': fid, 'A': lang, 'B': B, 'checks': {}, 'failures': []}

    def fail(c, detail):
        rec['checks'][c] = 'FAIL'
        rec['failures'].append({'check': c, 'detail': detail})

    def ok(c):
        rec['checks'].setdefault(c, 'PASS')

    exp = TOOLS[B][1]
    sem_only = G.envelope(sem)
    rc, E, err = run.tool(exp, env)
    mode = 'direct'
    if B == 'c' and lang != 'c' and rc == 68:
        # G1 c-export (translation-v1 section 6) accepts the C member only; the validator applies its own R.
        mode = 'validator-R(U_A, C)'
        env, sem_only = G.envelope(N(sem)), G.envelope(N(sem))
        rc, E, err = run.tool(exp, env)
    rec['export_input'] = mode
    t = root_tree(ops)
    bd = bounds(B, t, n)
    if bd['over']:
        rec['expect'] = 'LIMIT'
        if rc == 67 and E == b'':
            ok('export-limit-67')
            rec['result'] = 'PASS_LIMIT'
        else:
            fail('export-limit-67', {'status': rc, 'bounds': bd})
            rec['result'] = 'FAIL'
        return rec, None
    if rc != 0 or not E:
        fail('export', {'status': rc, 'stderr': err.decode(errors='replace')[:200], 'bounds': bd})
        rec['result'] = 'FAIL'
        return rec, None
    text = E.decode(errors='replace')
    rec['src_B_sha256'] = sha(E)
    # 4.3 generated, not replayed
    rc2, E2, _ = run.tool(exp, env)
    if (rc2, E2) == (rc, E):
        ok('export-deterministic')
    else:
        fail('export-deterministic', 'two runs differ')
    rc3, E3, _ = run.tool(exp, sem_only)
    if (rc3, E3) == (0, E):
        ok('4.3-semantic-only-identical')
    else:
        fail('4.3-semantic-only-identical', {'status': rc3})
    if mode == 'direct':
        for M in LANGS:
            rcm, Em, _ = run.tool(exp, G.envelope(R(sem, M)))
            if (rcm, Em) == (0, E):
                ok('4.3-R-%s-identical' % M)
            elif B == 'c' and M != 'c' and rcm == 68:
                # the G1 C exporter (translation-v1 section 6) accepts the C member only; NOT_RUN until the
                # #622 family extension lands (then validate_g1 requires acceptance)
                rec['checks']['4.3-R-%s-identical' % M] = 'NOT_RUN'
            else:
                fail('4.3-R-%s-identical' % M, {'status': rcm})
    else:
        rec['checks']['4.3-R-members-identical'] = 'NOT_RUN'
    if text == render(B, t, n):
        ok('export-form')
    else:
        fail('export-form', {'expected': render(B, t, n), 'got': text})
    # recorded only: a generated text may coincide with a source written in the exporter's own form
    # (F1-P30 `return 7ULL;`); section 4.3 is decided by the semantic-only and retargeted exports above
    rec['src_B_equals_source_bytes'] = E == src
    # import_B
    rc, envB, err = run.imp(B, E)
    if rc != 0:
        fail('import_B', {'status': rc, 'stderr': err.decode(errors='replace')[:200]})
    else:
        try:
            semB, sideB = G.decode_envelope(envB)
            mB, opsB = decode_family(semB)
            G.decode_sidecar(sideB, semB, len(opsB), E)
            if mB != B:
                raise Bad('importer %s emitted member %s' % (B, mB))
            if G.normalized_root(opsB) is None and DECLARES_PAIR[B]:
                raise Bad('import_B is not NORMALIZED although the importer declares a pair')
            if N(strip(semB, opsB, B)) == N(strip(sem, ops, lang)):
                ok('4.1-identity')
            else:
                fail('4.1-identity', {'N_strip_U_A': G.identity(N(strip(sem, ops, lang))),
                                      'N_strip_U_B': G.identity(N(strip(semB, opsB, B)))})
            if B == lang:
                rec['same_language_semantic_bytes_equal'] = semB == sem
            m = next(({'vector': [str(x) for x in v], 'reference': str(w), 'got': str(G.evaluate(opsB, v))}
                      for v, w in zip(vs, ref) if G.evaluate(opsB, v) != w), None)
            (fail('U_B-vs-reference', m) if m else ok('U_B-vs-reference'))
        except Bad as e:
            fail('import_B-decode', str(e))
    # 4.2 DIFFERENTIAL legs
    legs = {}
    for leg, res in (('oracle_A', U_A['oracle']), ('oracle_B', run.oracle(B, E, 'calc', n, vs, base)),
                     ('native', U_A['native'][:2])):
        if res[0] is None:
            legs[leg] = 'FAIL'
            fail('4.2-' + leg, res[1])
        else:
            m = G.compare(ref, res[0], vs)
            legs[leg] = 'FAIL' if m else 'PASS'
            if m:
                fail('4.2-' + leg, m)
            else:
                ok('4.2-' + leg)
    legs['reference'] = 'PASS'
    rec['legs'] = legs
    ev['leg_comparisons'] += len(vs) * 4
    rec['result'] = 'FAIL' if rec['failures'] else 'PASS'
    return rec, (E, t)


# ---------------------------------------------------------------- mutants of exporter outputs

def mutants(run, ev, outputs):
    """outputs: {(B, text): (ops, n, vs, ref, base)}. Every mutant of an exporter output goes through the
    checks the real output passed; it is killed when at least one detects it."""
    rows = []
    for (B, text), (ops, n, vs, ref, base) in outputs.items():
        if B == 'c':
            muts = [(nm, fn(text)) for nm, fn in G.MUT.C_OPERATORS]
        else:
            try:
                t = parse_rust(text) if B == 'rust' else parse_python(text)
            except Bad as e:
                rows.append({'B': B, 'operator': 'parse', 'killed': False, 'error': str(e)})
                continue
            if tree_text(B, t, n) != text:
                rows.append({'B': B, 'operator': 'parse', 'killed': False, 'error': 'parse/render not exact'})
                continue
            muts = []
            for nm, fn in TREE_MUTANTS[B]:
                m = fn(clone(t))
                muts.append((nm, tree_text(B, m, n) if m is not None else None))
        sem0 = N(strip(G.build_sem(ops), ops, 'c'))
        for nm, mt in muts:
            if mt is None or mt == text:
                continue
            det = []
            if mt != render(B, root_tree(ops), n):
                det.append('export-form')
            res = run.oracle(B, mt.encode(), 'calc', n, vs, base + '-mut')
            if res[0] is None:
                det.append('oracle_B-failed')
            elif G.compare(ref, res[0], vs):
                det.append('oracle_B-vs-reference')
            rc, envB, _ = run.imp(B, mt.encode())
            if rc != 0:
                det.append('import_B-refused-%s' % rc)
            else:
                try:
                    semB, _ = G.decode_envelope(envB)
                    mB, opsB = decode_family(semB)
                    if N(strip(semB, opsB, mB)) != sem0:
                        det.append('4.1-identity')
                    if any(G.evaluate(opsB, v) != w for v, w in zip(vs, ref)):
                        det.append('U_B-vs-reference')
                except Bad:
                    det.append('import_B-decode')
            rows.append({'B': B, 'operator': nm if B == 'c' else '%s-%s' % (B, nm), 'mutant_sha256': sha(mt.encode()), 'detected_by': det,
                         'killed': bool(det), 'killed_beyond_form': bool([x for x in det if x != 'export-form'])})
    by = {}
    for r in rows:
        b = by.setdefault(r['operator'], {'total': 0, 'killed': 0, 'killed_beyond_form': 0, 'form_only': 0})
        b['total'] += 1
        b['killed'] += r['killed']
        b['killed_beyond_form'] += r.get('killed_beyond_form', False)
        b['form_only'] += r['killed'] and not r.get('killed_beyond_form', False)
    ev['mutants'] = {'total': len(rows), 'killed': sum(r['killed'] for r in rows),
                     'killed_beyond_form': sum(r.get('killed_beyond_form', False) for r in rows),
                     'by_operator': by, 'rows': rows}


# ---------------------------------------------------------------- refusals

def outside_family(base_ops):
    """Valid source-aware v1 units outside the S1 family (compose section 1): every exporter refuses 68."""
    u64 = lambda obl: (lambda op: {9: (1, 0, 0, 0, 1), 1: (1, 1, 0, 0, 0)}.get(op, (1, 1, 0, 0, obl)))
    big = lambda op: {9: (2, 0, 0, 0, 1), 1: (2, 2, 0, 0, 0)}.get(op, (2, 2, 20, 255, 8))
    div = [(9, U, U, 0), (9, U, U, 1), (5, 0, 1, 0), (1, U, U, 0), (2, 2, 3, 0)]
    divf = lambda arith: (lambda op: {9: (1, 0, 0, 0, 1), 1: (1, 1, 0, 0, 0), 5: (1, 1, 0, 0, arith | 2)}.get(op, (1, 1, 0, 0, arith)))
    return [('cpython-bigint-profile-3', G.build_sem(base_ops, lang=3, prof=3, runtime=1, abi=1, fields=big)),
            ('osc-profile-5', G.build_sem(base_ops, lang=4, prof=5, fields=u64(1))),
            ('python-dynamic-profile-6', G.build_sem(base_ops, lang=3, prof=6, runtime=1, abi=2, fields=u64(5))),
            ('c-member-division', G.build_sem(div, lang=1, prof=1, fields=divf(1))),
            ('rust-member-division', G.build_sem(div, lang=2, prof=2, fields=divf(1))),
            ('python-member-division', G.build_sem(div, lang=3, prof=4, fields=divf(5)))]


def refusals(run, ev, base_env, base_ops):
    rows = []

    def expect(tool, label, data, args, want):
        rc, out, err = run.tool(tool, data, *args)
        rows.append({'tool': tool, 'case': label, 'want': want, 'status': rc, 'stdout_bytes': len(out),
                     'stderr': err.decode(errors='replace')[:100],
                     'result': 'NOT_RUN' if rc == 'absent' else 'PASS' if rc == want and out == b'' else 'FAIL'})

    for label, sem in outside_family(base_ops):
        e = G.envelope(sem)
        rc, out, _ = G.run([run.a.sa], b'\x02' + e)
        if rc != 0 or out != e:
            rows.append({'tool': 'sa', 'case': label, 'status': rc, 'result': 'FAIL',
                         'detail': 'probe is not a valid source-aware v1 unit, so it cannot test 68'})
            continue
        for tool in ('c-export', 'rust-export', 'python-export', 'lower'):
            expect(tool, 'outside-family ' + label, e, [], 68)
    for label, bad in G.malformed_envelopes(base_env):
        for tool in ('rust-export', 'python-export'):
            expect(tool, 'malformed ' + label, bad, [], 65)
    expect('rust-export', 'any argument', base_env, ['calc'], 64)
    for nm in ['9x', 'Calc', 'def', 'a-b', 'x' * 33]:
        expect('python-export', 'bad name %r' % nm[:12], base_env, [nm], 64)
    for lang in ('rust', 'python'):
        for rid, src, want, sha_ok in lane_refusals(lang):
            if not sha_ok:
                rows.append({'tool': TOOLS[lang][0], 'case': rid, 'result': 'FAIL', 'detail': 'fixture sha256'})
                continue
            expect(TOOLS[lang][0], rid, src, [], want)
    ev['refusals'] = {'total': len(rows), 'pass': sum(r['result'] == 'PASS' for r in rows),
                      'fail': sum(r['result'] == 'FAIL' for r in rows),
                      'not_run': sum(r['result'] == 'NOT_RUN' for r in rows), 'rows': rows}


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', default=str(ROOT), help='checkout holding tools/adapters/* and their builds')
    ap.add_argument('--omc', required=True)
    ap.add_argument('--bridge-native', required=True)
    ap.add_argument('--bridge-repo', default=os.environ.get('OMEGA_BRIDGE_REPO'),
                    help='omega-bridge clone (default $OMEGA_BRIDGE_REPO; no host default)')
    ap.add_argument('--sa', required=True, help='#621 native sa binary (confirms refusal probes are valid v1)')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--rustc', default='rustc')
    ap.add_argument('--python', default=sys.executable)
    ap.add_argument('--no-mutants', action='store_true')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'work').mkdir()
    repo = Path(a.repo).resolve()
    ev = {'schema': 'omega.universal.translation.validate.compose.v1',
          'contract': {'path': COMPOSE[0], 'sha256': COMPOSE[1]},
          'validator_sha256': sha(Path(__file__).read_bytes()), 'validate_g1_sha256': sha((HERE / 'validate_g1.py').read_bytes()),
          'evidence_class': 'REFERENCE + DIFFERENTIAL, NATIVE (host AArch64); no PROOF', 'leg_comparisons': 0}

    def not_run(why):
        ev['result'] = 'NOT_RUN'
        ev['reason'] = why
        (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
        print('NOT_RUN', why)
        return 3

    pins = {}
    for rel, want in [COMPOSE, G.CONTRACT] + list(G.FROZEN.items()):
        p = ROOT / rel
        got = sha(p.read_bytes()) if p.is_file() else None
        if got != want:
            return not_run('%s sha256 %s, pinned %s' % (rel, got, want))
        pins[rel] = got
    for lang, rel in LANE_SPECS.items():
        p = ROOT / rel
        pins[rel] = sha(p.read_bytes()) if p.is_file() else None
    ev['pins'] = pins
    if not a.bridge_repo:
        return not_run('no omega-bridge clone: pass --bridge-repo or set OMEGA_BRIDGE_REPO')
    for exe in (a.omc, a.bridge_native, a.sa):
        if not Path(exe).is_file():
            return not_run('absent: %s' % exe)
    if sha(Path(a.omc).read_bytes()) != G.OMC_SHA:
        return not_run('omc is not the translation-v1 section 1 pin %s' % G.OMC_SHA)
    rc, nsrc, _ = G.run(['git', '-C', a.bridge_repo, 'show', G.BRIDGE_COMMIT + ':src/native.om'])
    if rc != 0 or G.run([a.omc], nsrc, timeout=120)[1] != Path(a.bridge_native).read_bytes():
        return not_run('bridge generator is not omc(native.om @ %s)' % G.BRIDGE_COMMIT)
    tools, tinfo = {}, {}
    for name in ['lower'] + [t for p in TOOLS.values() for t in p]:
        p = repo / 'tools/adapters' / name
        if p.is_file() and os.access(p, os.X_OK):
            tools[name] = str(p)
            tinfo[name] = {'path': str(p), 'sha256': sha(p.read_bytes())}
            for bj in sorted(repo.glob('build/**/build.json')):
                if (bj.parent / name).is_file():
                    tinfo[name].update({'build': str(bj.parent.relative_to(repo)), 'build_json_sha256': sha(bj.read_bytes()),
                                        'binary_sha256': sha((bj.parent / name).read_bytes())})
        else:
            tinfo[name] = 'ABSENT'
    head = G.run(['git', '-C', repo, 'rev-parse', 'HEAD'])
    ev['repo'] = {'path': str(repo), 'head': head[1].decode().strip() if head[0] == 0 else None}
    ev['tools'] = tinfo
    ev['toolchains'] = {k: G.run(v)[1].decode().split('\n')[0] for k, v in
                        {'cc': [a.cc, '--version'], 'rustc': [a.rustc, '--version'], 'python': [a.python, '--version']}.items()}
    if 'lower' not in tools:
        return not_run('tools/adapters/lower absent: no native realization')
    try:
        fixtures, nip, finfo = load_fixtures()
    except Bad as e:
        return not_run('fixtures: %s' % e)
    ev['fixtures'] = finfo
    ev['not_in_profile'] = nip
    args = SimpleNamespace(omc=a.omc, bridge_native=a.bridge_native, cc=a.cc, rustc=a.rustc, python=a.python,
                           sa=a.sa, tools=tools)
    run = Run(args, out)

    # U_A per fixture, with the A oracle, reference and native legs (shared by every composition from it)
    units, frows = [], []
    for fx in fixtures:
        lang, fid, base, src, name, n = fx
        imp, exp = TOOLS[lang]
        row = {'fixture': fid, 'A': lang}
        if imp not in tools:
            row['result'] = 'NOT_RUN'
            frows.append(row)
            units.append((fx, None))
            continue
        rc, env, err = run.imp(lang, src)
        try:
            if rc != 0:
                raise Bad('import_%s exit %s %s' % (lang, rc, err.decode(errors='replace')[:120]))
            sem, side = G.decode_envelope(env)
            m, ops = decode_family(sem)
            G.decode_sidecar(side, sem, len(ops), src)
            if m != lang:
                raise Bad('importer %s emitted member %s' % (lang, m))
            if DECLARES_PAIR[lang] and G.normalized_root(ops) is None:
                raise Bad('import is not NORMALIZED although the importer declares a pair')
            if G.arity(ops) != n:
                raise Bad('arity %d != fixture %d' % (G.arity(ops), n))
        except Bad as e:
            row.update({'result': 'FAIL', 'failure': str(e)})
            frows.append(row)
            units.append((fx, None))
            continue
        vs = G.vectors(n, base)
        ref = [G.evaluate(ops, v) for v in vs]
        U_A = {'env': env, 'sem': sem, 'ops': ops, 'vs': vs, 'ref': ref,
               'oracle': run.oracle(lang, src, name, n, vs, base), 'native': run.native(N(sem), n, vs, base)}
        if lang != 'c':
            rc, _, _ = run.tool('lower', env)
            row['lower_direct_status'] = rc  # translation-v1 section 5 / coordinator ruling: lower is C-only (68)
            if rc != 68:
                row.setdefault('failures', []).append('lower accepted or mis-refused a non-C member: %s' % rc)
        row.update({'semantic_identity': G.identity(sem), 'N_identity': G.identity(N(strip(sem, ops, lang))),
                    'vectors': len(vs), 'result': 'FAIL' if row.get('failures') else 'PASS'})
        frows.append(row)
        units.append((fx, U_A))
    # F2 fidelity: every F2 rewrite evaluates like its F1 source (compose section 5); N equality recorded
    byid = {fx[1]: u for fx, u in units}
    fid_rows = []
    for fx, u in units:
        if fx[0] == 'c' or u is None or not fx[1].startswith('F2-'):
            continue
        c = byid.get(fx[2])
        if c is None:
            continue
        same = all(G.evaluate(u['ops'], v) == G.evaluate(c['ops'], v) for v in u['vs'])
        fid_rows.append({'fixture': fx[1], 'f1': fx[2], 'values_equal': same,
                         'N_strip_equal_to_C_import': N(strip(u['sem'], u['ops'], fx[0])) == N(strip(c['sem'], c['ops'], 'c')),
                         'result': 'PASS' if same else 'FAIL'})
    ev['f2_fidelity'] = {'total': len(fid_rows), 'pass': sum(r['result'] == 'PASS' for r in fid_rows),
                         'N_equal': sum(r['N_strip_equal_to_C_import'] for r in fid_rows), 'rows': fid_rows}
    ev['fixture_imports'] = frows

    comps, outputs = [], {}
    for fx, U_A in units:
        for B in LANGS:
            key = '%s->%s' % (fx[0], B)
            if U_A is None or TOOLS[B][1] not in tools or TOOLS[B][0] not in tools:
                # an A import that failed is already a FAIL of that fixture; a missing tool is NOT_RUN
                imported_failed = any(r['fixture'] == fx[1] and r.get('result') == 'FAIL' for r in frows)
                comps.append({'fixture': fx[1], 'A': fx[0], 'B': B, 'composition': key,
                              'result': 'FAIL' if imported_failed else 'NOT_RUN'})
                continue
            rec, art = compose(run, ev, fx, B, U_A)
            rec['composition'] = key
            comps.append(rec)
            if art:
                outputs.setdefault((B, art[0].decode(errors='replace')), (U_A['ops'], fx[5], U_A['vs'], U_A['ref'], fx[2]))
        print(fx[1], ' '.join('%s:%s' % (c['B'], c['result']) for c in comps[-3:]), flush=True)
    summary = {}
    for c in comps:
        s = summary.setdefault(c['composition'], {'PASS': 0, 'PASS_LIMIT': 0, 'FAIL': 0, 'NOT_RUN': 0,
                                                  'export_input': set()})
        s[c['result']] += 1
        if 'export_input' in c:
            s['export_input'].add(c['export_input'])
    for s in summary.values():
        s['export_input'] = sorted(s['export_input'])
    ev['compositions'] = {k: v for k, v in summary.items() if k.split('->')[0] != k.split('->')[1]}
    ev['same_language_round_trips'] = {k: v for k, v in summary.items() if k.split('->')[0] == k.split('->')[1]}
    legs = {}
    for c in comps:
        for leg, r in c.get('legs', {}).items():
            legs.setdefault(c['composition'], {}).setdefault(leg, {'PASS': 0, 'FAIL': 0})[r] += 1
        for chk, r in c.get('checks', {}).items():
            legs.setdefault(c['composition'], {}).setdefault(chk, {'PASS': 0, 'FAIL': 0, 'NOT_RUN': 0})
            legs[c['composition']][chk][r] = legs[c['composition']][chk].get(r, 0) + 1
    ev['per_check'] = legs
    base = next((u for fx, u in units if fx[1] == 'F1-P05'), None)
    if base:
        refusals(run, ev, base['env'], base['ops'])
    if not a.no_mutants:
        mutants(run, ev, outputs)
    ev['cases'] = comps
    failed = ['%s %s' % (c['composition'], c['fixture']) for c in comps if c['result'] == 'FAIL']
    failed += ['import %s' % r['fixture'] for r in frows if r['result'] == 'FAIL']
    failed += ['f2-fidelity %s' % r['fixture'] for r in fid_rows if r['result'] != 'PASS']
    failed += ['refusal %s %s' % (r['tool'], r['case']) for r in ev.get('refusals', {}).get('rows', []) if r['result'] == 'FAIL']
    if 'mutants' in ev and ev['mutants']['killed'] != ev['mutants']['total']:
        failed.append('mutants survived: %d' % (ev['mutants']['total'] - ev['mutants']['killed']))
    if not base:
        failed.append('F1-P05 did not import: refusal probes not run')
    notrun = [c for c in comps if c['result'] == 'NOT_RUN'] + [r for r in ev.get('refusals', {}).get('rows', []) if r['result'] == 'NOT_RUN']
    # a check that could not run (G1 C exporter refusing family members; validator-applied R) keeps the
    # composition result but makes the whole run incomplete: never a plain PASS
    notrun += ['%s %s %s' % (c['composition'], c['fixture'], k) for c in comps for k, v in c.get('checks', {}).items() if v == 'NOT_RUN']
    ev['not_run_checks'] = len(notrun)
    ev['failed'] = failed
    ev['result'] = 'FAIL' if failed else 'NOT_RUN' if notrun else 'PASS'
    (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
    print(json.dumps({'result': ev['result'], 'compositions': ev['compositions'],
                      'same_language_round_trips': ev['same_language_round_trips'],
                      'f2_fidelity': '%d/%d (N equal %d)' % (ev['f2_fidelity']['pass'], ev['f2_fidelity']['total'], ev['f2_fidelity']['N_equal']),
                      'refusals': {k: ev['refusals'][k] for k in ('total', 'pass', 'fail', 'not_run')} if 'refusals' in ev else 'NOT_RUN',
                      'mutants': '%d/%d killed (%d beyond the form check)' % (ev['mutants']['killed'], ev['mutants']['total'], ev['mutants']['killed_beyond_form']) if 'mutants' in ev else 'NOT_RUN',
                      'failed': failed[:40]}, indent=1))
    return 1 if failed else 3 if notrun else 0


if __name__ == '__main__':
    raise SystemExit(main())
