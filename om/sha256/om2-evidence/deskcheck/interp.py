#!/usr/bin/env python3
# L838 desk check (oracle, outside the delivered closure, NOT compiler evidence): a small interpreter
# of the OM-2 subset that om/sha256/sha256-om2.om uses, as the contract describes it, to check the
# unit's algorithm (schedule, rounds, buffering, padding) against hashlib before an OM-2 omc exists.
import sys, hashlib, random
W = {'uchar': 8, 'ushort': 16, 'uint': 32, 'ulong': 64}
BIN = {'add', 'sub', 'mul', 'shl', 'shr', 'and', 'or', 'xor', 'rotr', 'lt', 'le', 'gt', 'ge', 'eq', 'ne'}
class Trap(Exception): pass

class Arr:
    def __init__(s, t, n, v=0): s.t, s.a = t, [v] * n
class View:  # a (possibly unsized) array argument
    def __init__(s, arr): s.arr = arr

def parse(src):
    lines = src.split('\n'); fns = {}; layouts = {}; i = 0
    while i < len(lines):
        w = lines[i].split()
        if not w or w[0] == 'note' or w == ['om2']: i += 1; continue
        if w[0] == 'layout':
            name, size = w[1], int(w[2]); fields = {}; i += 1
            while lines[i].split()[0] != 'end':
                f = lines[i].split()  # field F T COUNT at OFF little
                if f[3] == 'at': fields[f[1]] = (f[2], None, int(f[4]))  # E5: no COUNT = scalar
                else: fields[f[1]] = (f[2], int(f[3]), int(f[5]))
                i += 1
            layouts[name] = (size, fields); i += 1; continue
        exp = w[0] == 'export'
        if exp: w = w[1:]
        assert w[0] == 'fn', lines[i]
        name = w[1]; params = []; j = 2
        while w[j] != 'to':
            pn = w[j]
            if w[j + 1] in W:
                params.append((pn, 'scalar', w[j + 1])); j += 2
            elif w[j + 2] in layouts:
                params.append((pn, w[j + 1], w[j + 2])); j += 3
            else:
                params.append((pn, w[j + 1], w[j + 2], w[j + 3])); j += 4
                if j < len(w) and w[j] == 'fresh': j += 1
        body = []; i += 1; depth = 0
        while True:
            t = lines[i].split()
            if t and t[0] != 'note':
                if t[0] == 'end' and depth == 0: break
                if t[0] in ('for', 'if'): depth += 1
                if t[0] == 'end': depth -= 1
                body.append((i + 1, t))
            i += 1
        fns[name] = (params, body, w[j + 1]); i += 1
    return fns, layouts

class VM:
    def __init__(s, src):
        s.fns, s.layouts = parse(src)
    def call(s, name, args):
        params, body, rt = s.fns[name]
        env = {}
        for p, a in zip(params, args): env[p[0]] = a
        s.ret = False
        s.block(body, 0, len(body), env)
        s.ret = False
    def block(s, body, a, b, env):
        k = a
        while k < b and not s.ret:
            ln, t = body[k]
            end = s.match(body, k) if t[0] in ('for', 'if') else k
            if t[0] == 'for':
                var, ty = t[1], t[2]
                cnt, rest = s.expr(t[3:], env)
                for it in range(cnt[0]):
                    env[var] = (it, ty)
                    s.block(body, k + 1, end, env)
                    if s.ret: return
                k = end + 1; continue
            if t[0] == 'if':
                c, _ = s.expr(t[1:], env)
                if c[0]: s.block(body, k + 1, end, env)
                k = end + 1; continue
            s.stmt(t, env)
            k += 1
    def match(s, body, k):
        d = 0
        for j in range(k, len(body)):
            w = body[j][1][0]
            if w in ('for', 'if'): d += 1
            elif w == 'end':
                d -= 1
                if d == 0: return j
        raise SyntaxError
    def stmt(s, t, env):
        if t[0] == 'ret': s.ret = True; return
        if len(t) > 1 and t[1] == '=':
            if t[2] == 'new':
                env[t[0]] = Arr(t[3], int(t[4]), int(t[5])); return
            v, rest = s.expr(t[2:], env); assert not rest, t
            old = env.get(t[0])
            if old is not None and old[1] and v[1] is None: v = (v[0], old[1])
            env[t[0]] = v; return
        if t[0] == 'put':
            tgt = env[t[1]]
            if isinstance(tgt, View): tgt = tgt.arr
            if isinstance(tgt, Lay):
                f = t[2]; r = t[3:]
                if tgt.f[f][1] is None: i = (0, None)
                else: i, r = s.expr(r, env)
                v, r = s.expr(r, env); assert not r
                tgt.put(f, i[0], v[0]); return
            i, r = s.expr(t[2:], env); v, r = s.expr(r, env); assert not r, t
            if not (0 <= i[0] < len(tgt.a)): raise Trap(3)
            assert 0 <= v[0] < (1 << W[tgt.t]), (t, v)
            tgt.a[i[0]] = v[0]; return
        if t[0] in s.fns:
            args, r = s.args(t[0], t[1:], env); assert not r, t
            s.call(t[0], args); return
        raise SyntaxError(t)
    def args(s, fn, toks, env):
        out = []
        for p in s.fns[fn][0]:
            if p[1] == 'scalar':
                v, toks = s.expr(toks, env); out.append(v)
            else:
                if toks[0] in ('view', 'edit'): toks = toks[1:]
                a = env[toks[0]]; toks = toks[1:]
                out.append(a)
        return out, toks
    def expr(s, t, env):
        w = t[0]; r = t[1:]
        if w.startswith('0x'): return (int(w, 16), None), r
        if w.isdigit(): return (int(w), None), r
        if w in W:
            v, r = s.expr(r, env)
            if not (0 <= v[0] < (1 << W[w])): raise Trap(5)
            return (v[0], w), r
        if w == 'not':
            v, r = s.expr(r, env); return (v[0] ^ ((1 << W[v[1]]) - 1), v[1]), r
        if w == 'len':
            a = env[r[0]]; a = a.arr if isinstance(a, View) else a
            return (len(a.a), 'ulong'), r[1:]
        if w == 'get':
            a = env[r[0]]; a = a.arr if isinstance(a, View) else a; r = r[1:]
            if isinstance(a, Lay):
                f = r[0]; r = r[1:]
                if a.f[f][1] is None: return a.get(f, 0), r
                i, r = s.expr(r, env); return a.get(f, i[0]), r
            i, r = s.expr(r, env)
            if not (0 <= i[0] < len(a.a)): raise Trap(3)
            return (a.a[i[0]], a.t), r
        if w in BIN:
            x, r = s.expr(r, env); y, r = s.expr(r, env)
            ty = x[1] or y[1]
            assert not (x[1] and y[1] and x[1] != y[1]), (w, x, y)
            m = (1 << W[ty]) - 1 if ty else None
            a, b = x[0], y[0]
            if w in ('lt', 'le', 'gt', 'ge', 'eq', 'ne'):
                return (int({'lt': a < b, 'le': a <= b, 'gt': a > b, 'ge': a >= b, 'eq': a == b, 'ne': a != b}[w]), 'bool'), r
            if w in ('shl', 'shr', 'rotr') and not (0 <= b < W[ty]): raise Trap(7)
            if w == 'rotr': v = (a >> b) | (a << (W[ty] - b))
            else:
                v = {'add': a + b, 'sub': a - b, 'mul': a * b, 'shl': a << b if b < 4096 else 0, 'shr': a >> b,
                     'and': a & b, 'or': a | b, 'xor': a ^ b}[w]
            return (v & m, ty), r
        if w in env: return env[w], r
        raise SyntaxError(t)

class Lay:
    def __init__(s, fields, size, data=None):
        s.f, s.b = fields, bytearray(size) if data is None else data
    def get(s, f, i):
        t, n, off = s.f[f]; wb = W[t] // 8; n = 1 if n is None else n
        if not (0 <= i < n): raise Trap(3)
        return (int.from_bytes(s.b[off + i * wb: off + (i + 1) * wb], 'little'), t)
    def put(s, f, i, v):
        t, n, off = s.f[f]; wb = W[t] // 8; n = 1 if n is None else n
        if not (0 <= i < n): raise Trap(3)
        s.b[off + i * wb: off + (i + 1) * wb] = v.to_bytes(wb, 'little')

def main():
    vm = VM(open(sys.argv[1]).read())
    size, fields = vm.layouts['shactx']
    def ctx(): return Lay(fields, size)
    def view(bs): return View(_arr(bs))
    def _arr(bs):
        a = Arr('uchar', len(bs)); a.a = list(bs); return a
    def out(): return Arr('uchar', 32)
    rng = random.Random(838); bad = 0; cases = 0
    lens = list(range(0, 260)) + [447, 448, 511, 512, 1000, 4095, 4096, 4097, 12345]
    for n in lens:
        msg = bytes(rng.randrange(256) for _ in range(n)); ref = hashlib.sha256(msg).digest()
        o = out(); vm.call('sha256_hash', [view(msg), o]); cases += 1
        if bytes(o.a) != ref: bad += 1; print('hash FAIL', n)
        c = ctx(); vm.call('sha256_init', [c]); p = 0
        while p < n:
            k = rng.choice([0, 1, 3, 55, 56, 63, 64, 65, 200]); k = min(k, n - p)
            vm.call('sha256_update', [c, view(msg[p:p + k])]); p += k
            if k == 0: vm.call('sha256_update', [c, view(b'')])
        o = out(); vm.call('sha256_final', [c, o]); cases += 1
        if bytes(o.a) != ref: bad += 1; print('split FAIL', n)
        # copy of a live context (erratum E3): copy after the first half, continue both
        c1 = ctx(); vm.call('sha256_init', [c1]); vm.call('sha256_update', [c1, view(msg[:n // 2])])
        c2 = Lay(fields, size, bytearray(c1.b)); o1, o2 = out(), out()
        vm.call('sha256_update', [c1, view(msg[n // 2:])]); vm.call('sha256_update', [c2, view(msg[n // 2:])])
        vm.call('sha256_final', [c1, o1]); vm.call('sha256_final', [c2, o2]); cases += 1
        if not (bytes(o1.a) == bytes(o2.a) == ref): bad += 1; print('copy FAIL', n)
        # init/update/final after a final equals a fresh context
        vm.call('sha256_init', [c1]); vm.call('sha256_update', [c1, view(msg)]); o3 = out()
        vm.call('sha256_final', [c1, o3]); cases += 1
        if bytes(o3.a) != ref: bad += 1; print('reuse FAIL', n)
    print(f'desk-check cases={cases} fail={bad}')
    sys.exit(1 if bad else 0)
main()
