#!/usr/bin/env python3
"""Independent G3 composition validation (#623) against spec/universal/translation-compose-v2.md.

Six members of the extended S1 family: C, Rust, Python (OMSA version 1, v1-core tools) and C++, Zig, Go
(OMSA version 2, v2-codec tools). For every directed composition A -> B (30, A != B) and every fixture of A
(F1 + accepted C import controls; fixtures-f2-<lang>.json positives, controls and extra positives):
  U_A = import_A(src); src_B = export_B(U_A), or export_B(R(U_A, B)) through `retarget` when B is a v1-core
  exporter and A a version-2 member (section 3); U_B = import_B(src_B)
  4.1 N(strip(U_B)) == N(strip(U_A)) byte for byte
  4.2 A's oracle on src, this file's reference on U_A, B's oracle on src_B and lower(N(U_A)) -> pinned omc ->
      pinned bridge -> omega_calc agree on the translation-v1 section 3 vectors
  4.3 src_B is generated: the semantic-only input and R(U_A, M) for every member M export the same bytes
      (a v1-core exporter given a version-2 R(U_A, M) must refuse 65 with no stdout, section 3)
Same-language round trips are reported separately. R, N, strip, the member decoder, each exporter's frozen
output form, the bounds and the mutation operators are written here from the contracts; the adapters,
`retarget` and the codecs are exercised only through their public commands (their sources are not read).
`retarget` output is cross-checked byte for byte against this file's R for every fixture and member.
Oracles: cc (C17), rustc --edition 2021 -O -C overflow-checks=on, python3, g++ -std=c++17 (flags of
cpp-u64-v1.md section 5), zig 0.17.0 build-exe -O Debug (safety-checked), go 1.27.3 build (GOTOOLCHAIN=local).
Exit: 0 PASS, 1 FAIL, 3 NOT_RUN.
"""
from pathlib import Path
from types import SimpleNamespace
import argparse, hashlib, json, os, re, shutil, struct, subprocess, sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import validate_g1 as G  # noqa: E402
import validate_compose as C1  # noqa: E402

ROOT = G.ROOT
MASK = G.MASK
U = 65535
COMPOSE2 = ('spec/universal/translation-compose-v2.md', '74cc22f371072f7acf3c3376dfb2ebc865c5ef67846316aa37c1eb5d254af159')
SA2 = ('spec/universal/source-aware-v2.md', 'ea2e14654cbba902fca8c4e070e95bb3ada384c061ee20bcd87c67fff8a9ce1c')
LANE_SPECS = {l: 'spec/universal/%s-u64-v1.md' % l for l in ('rust', 'python', 'cpp', 'zig', 'go')}
LANGS = ['c', 'rust', 'python', 'cpp', 'zig', 'go']
V1CORE = {'c', 'rust', 'python'}
# compose-v2 section 1: OMSA version, language, profile, op dialect, arithmetic obligations
MEMBERS = {'c': (1, 1, 1, 1, 1), 'rust': (1, 2, 2, 2, 1), 'python': (1, 3, 4, 3, 5),
           'cpp': (2, 5, 7, 5, 1), 'zig': (2, 6, 8, 6, 1), 'go': (2, 7, 9, 7, 1)}
# compose-v1 section 3: every importer declares a normalization pair (c-u64-import-v1; rust/python/cpp/zig/go
# -u64-v1 section 3 "Normalization pair: yes")
DECLARES_PAIR = {l: True for l in LANGS}
TOOLS = {l: ('%s-import' % l, '%s-export' % l) for l in LANGS}
CXX_FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wsign-conversion',
             '-pedantic-errors', '-Wno-unused-parameter']
GO_ENV = {'GOTOOLCHAIN': 'local', 'GOFLAGS': '-mod=mod', 'GOPROXY': 'off', 'GOWORK': 'off', 'CGO_ENABLED': '0'}


class Bad(Exception):
    pass


def sha(b):
    return hashlib.sha256(b).hexdigest()


# ---------------------------------------------------------------- member decoder, R, N, strip

def decode_member(sem):
    if len(sem) < 44 or sem[:4] != b'OMSA' or sem[7] != 1:
        raise Bad('semantic header')
    count = struct.unpack_from('<H', sem, 10)[0]
    if not 1 <= count <= 64 or len(sem) != 44 + 20 * count or sem[12:44] != G.SEMANTICS_DIGEST:
        raise Bad('count/length/digest')
    member = next((m for m, v in MEMBERS.items() if (sem[4], sem[5], sem[6]) == v[:3]), None)
    if member is None or (sem[8], sem[9]) != (0, 1):
        raise Bad('not an extended-family header %r' % (tuple(sem[4:10]),))
    dialect, arith = MEMBERS[member][3:]
    ops, slots = [], []
    for k in range(count):
        d, v, op, t, attr, eff, alias, obl, a, b, imm = struct.unpack_from('<8BHHQ', sem, 44 + 20 * k)
        if (d, v, t, eff, alias) != (dialect, 1, 1, 0, 0):
            raise Bad('record %d fields' % k)
        if op == 9 and (attr, obl, a, b) == (0, 1, U, U) and imm <= 5:
            slots.append(imm)
        elif op == 1 and (attr, obl, a, b) == (1, 0, U, U):
            pass
        elif op in (2, 3, 4) and (attr, obl, imm) == (1, arith, 0) and a < k and b < k:
            pass
        else:
            raise Bad('record %d op %d' % (k, op))
        ops.append((op, a, b, imm))
    if sorted(slots) != list(range(len(slots))):
        raise Bad('input slots')
    return member, ops


def R(sem, member):
    ver, lang, prof, dialect, arith = MEMBERS[member]
    b = bytearray(sem)
    b[4], b[5], b[6] = ver, lang, prof
    for k in range(struct.unpack_from('<H', sem, 10)[0]):
        o = 44 + 20 * k
        b[o] = dialect
        if b[o + 2] in (2, 3, 4):
            b[o + 7] = arith
    return bytes(b)


def N(sem):
    return R(sem, 'c')


def strip(sem, ops, member):
    if not DECLARES_PAIR[member] or G.normalized_root(ops) is None:
        return sem
    c = len(ops) - 2
    return sem[:10] + struct.pack('<H', c) + sem[12:44 + 20 * c]


# ---------------------------------------------------------------- frozen output forms of the new exporters

def cprec(t, leaf, sym):
    """Infix rendering with the translation-v1 section 6 parenthesis rule (leaf 3, * 2, + - 1)."""
    if t['t'] != 'op':
        return leaf(t), 3
    p = 2 if t['op'] == '*' else 1
    (lt, lp), (rt, rp) = cprec(t['l'], leaf, sym), cprec(t['r'], leaf, sym)
    if lp < p:
        lt = '(' + lt + ')'
    if rp <= p:
        rt = '(' + rt + ')'
    return '%s %s %s' % (lt, sym(t), rt), p


def cpp_leaf(t):
    if t['t'] == 'in':
        return 'i%d' % t['s']
    return ('%dULL' if t.get('bare') else 'std::uint64_t{%dULL}') % t['v']


def zig_text(t):
    """cpp-like rule; Zig symbols +% -% *%; a left constant operand of a constant-constant op is @as(u64, N)."""
    def leaf(x):
        return 'x%d' % x['s'] if x['t'] == 'in' else '%d' % x['v']

    def walk(x):
        if x['t'] != 'op':
            return leaf(x), 3
        p = 2 if x['op'] == '*' else 1
        (lt, lp), (rt, rp) = walk(x['l']), walk(x['r'])
        if x['l']['t'] == 'k' and x['r']['t'] == 'k':
            lt = '@as(u64, %d)' % x['l']['v']
        if lp < p:
            lt = '(' + lt + ')'
        if rp <= p:
            rt = '(' + rt + ')'
        return '%s %s %s' % (lt, x['op'] if x['plain'] else x['op'] + '%', rt), p
    return walk(t)[0]


def slots_of(t):
    return {y['s'] for y in C1.preorder(t) if y['t'] == 'in'}


def render(lang, t, n, run):
    if lang in V1CORE:
        return C1.render(lang, t, n)
    if lang == 'cpp':
        params = ', '.join('std::uint64_t i%d' % s for s in range(n))
        return '#include <cstdint>\nstd::uint64_t calc(%s) { return %s; }\n' % (params, cprec(t, cpp_leaf, lambda x: x['op'])[0])
    if lang == 'zig':
        used = slots_of(t)
        params = ', '.join(('x%d: u64' % s) if s in used else '_: u64' for s in range(n))
        return 'pub fn calc(%s) u64 {\n    return %s;\n}\n' % (params, zig_text(t))
    expr = cprec(t, lambda x: 'i%d' % x['s'] if x['t'] == 'in' else '%d' % x['v'], lambda x: x['op'])[0]
    params = ', '.join('i%d uint64' % s for s in range(n))
    return run.gofmt('package main\n\nfunc calc(%s) uint64 {\n\treturn %s\n}\n' % (params, expr))


def bounds(lang, t, n, run):
    """True iff the exporter must refuse 67 (re-import bounds of each profile spec)."""
    if lang in V1CORE:
        return C1.bounds(lang, t, n)
    text = render(lang, t, n, run)
    nodes = sum(1 for y in C1.preorder(t) if y['t'] != 'in')
    body = text.split('return ', 1)[1]
    depth = best = 0
    for ch in body:
        depth += {'(': 1, ')': -1}.get(ch, 0)
        best = max(best, depth)
    over = len(text.encode()) > 4096 or n + nodes + 2 > 64 or best > 16
    if lang == 'go':
        def exact(x):  # exact value of a constant-only node, None if it has an input (go-u64-v1 section 1.1)
            if x['t'] == 'in':
                return None
            if x['t'] == 'k':
                return x['v']
            a, b = exact(x['l']), exact(x['r'])
            if a is None or b is None:
                return None
            return {'+': a + b, '-': a - b, '*': a * b}[x['op']]
        over = over or any(x['t'] == 'op' and exact(x) is not None and not 0 <= exact(x) <= MASK for x in C1.preorder(t))
    return {'bytes': len(text.encode()), 'reimport_ops': n + nodes + 2, 'depth': best, 'over': over}


# ---------------------------------------------------------------- oracles (zig deferred and batched)

CPP_DRIVER = '''
#include <cstdio>
int main()
{
    std::uint64_t n = 0;
    if (std::fread(&n, 8, 1, stdin) != 1)
        return 2;
    std::uint64_t v[6] = {0, 0, 0, 0, 0, 0};
    for (std::uint64_t j = 0; j < n; j++) {
        if (std::fread(v, 8, std::size_t{%(k)d}, stdin) != std::size_t{%(k)d})
            return 2;
        std::printf("0 %%llu\\n", static_cast<unsigned long long>(%(name)s(%(args)s)));
    }
    return 0;
}
'''
GO_DRIVER = '''package main

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"os"
)

func oracleRead(d []byte, o uint64) uint64 { return binary.LittleEndian.Uint64(d[o : o+8]) }

func main() {
	d, _ := io.ReadAll(os.Stdin)
	n := oracleRead(d, 0)
	w := bufio.NewWriter(os.Stdout)
	for j := uint64(0); j < n; j++ {
		base := 8 + 8*%(k)d*j
		_ = base
		fmt.Fprintf(w, "0 %%d\\n", %(name)s(%(args)s))
	}
	w.Flush()
}
'''


class Run(C1.Run):
    def __init__(self, a, out):
        super().__init__(a, out)
        self.res = {}
        self.zig_pending = {}
        self.toolcache = {}
        self.fmtcache = {}

    def tool(self, name, data, *args, fresh=False):
        k = (name, sha(data), args)
        if fresh or k not in self.toolcache:
            path = self.a.tools.get(name)
            r = ('absent', b'', b'') if path is None else G.run([path] + list(args), data)
            if fresh:
                return r
            self.toolcache[k] = r
        return self.toolcache[k]

    def imp(self, lang, src):
        return self.tool(TOOLS[lang][0], src)

    def gofmt(self, text):
        if text not in self.fmtcache:
            r = subprocess.run([self.a.gofmt], input=text.encode(), capture_output=True, timeout=60)
            self.fmtcache[text] = r.stdout.decode() if r.returncode == 0 else text
        return self.fmtcache[text]

    def oracle_key(self, lang, src, name, n, vs, key):
        """Schedule or run B's oracle; the verdict is read later from self.res[k] = (rows, why)."""
        k = (lang, sha(src), name, n, key)
        if k in self.res or k in self.zig_pending:
            return k
        if lang in V1CORE:
            self.res[k] = C1.Run.oracle(self, lang, src, name, n, vs, key)
        elif lang == 'cpp':
            d = self.ctx.tmp('oracle-cpp-%s' % key)
            drv = CPP_DRIVER % {'k': n, 'name': name, 'args': ', '.join('v[%d]' % i for i in range(n))}
            (d / 'prog.cpp').write_bytes(src + b'\n' + drv.encode())
            rc, _, err = G.run([self.a.cxx] + CXX_FLAGS + ['-o', d / 'run', d / 'prog.cpp'], timeout=180)
            self.res[k] = (None, 'g++ exit %s: %s' % (rc, err.decode(errors='replace')[:300])) if rc != 0 else \
                self.collect(G.run([d / 'run'], G.pack_vectors(vs, n), timeout=120), vs)
        elif lang == 'go':
            d = self.ctx.tmp('oracle-go-%s' % key)
            (d / 'go.mod').write_text('module oracle\n\ngo 1.27\n')
            (d / 'unit.go').write_bytes(src)
            (d / 'main.go').write_text(GO_DRIVER % {'k': n, 'name': name, 'args': ', '.join(
                'oracleRead(d, base+%d)' % (8 * i) for i in range(n))})
            env = dict(os.environ, **GO_ENV, GOCACHE=str(self.ctx.work / 'go-cache'), GOPATH=str(self.ctx.work / 'gopath'))
            r = subprocess.run([self.a.go, 'build', '-trimpath', '-o', str(d / 'run'), '.'], cwd=d, env=env,
                               capture_output=True, timeout=300)
            self.res[k] = (None, 'go build exit %s: %s' % (r.returncode, r.stderr.decode(errors='replace')[:300])) \
                if r.returncode != 0 else self.collect(G.run([d / 'run'], G.pack_vectors(vs, n), timeout=120), vs)
        else:
            self.zig_pending[k] = (src, name, n, vs)
        return k

    def flush_zig(self, batch=60):
        items = list(self.zig_pending.items())
        self.zig_pending = {}
        for i in range(0, len(items), batch):
            self.zig_batch(items[i:i + batch])

    def zig_batch(self, items):
        d = self.ctx.tmp('oracle-zig-batch')
        main = ['const std = @import("std");', 'extern "c" fn getenv(name: [*:0]const u8) ?[*:0]const u8;']
        prongs = []
        for j, (k, (src, name, n, vs)) in enumerate(items):
            (d / ('u%d.zig' % j)).write_bytes(src)
            rows = ', '.join('.{ %s }' % ', '.join(str(x) for x in v) for v in vs)
            main.append('const unit_%d = @import("u%d.zig");' % (j, j))  # `u<digits>` would shadow a Zig primitive
            main.append('const V%d = [_][%d]u64{ %s };' % (j, n, rows))
            cap = 'v' if n else '_'
            prongs.append('        %d => for (V%d) |%s| std.debug.print("0 {d}\\n", .{unit_%d.%s(%s)}),' % (
                j, j, cap, j, name, ', '.join('v[%d]' % i for i in range(n))))
        main += ['pub fn main() void {',
                 '    const sel = std.fmt.parseInt(usize, std.mem.span(getenv("ORACLE_UNIT").?), 10) catch unreachable;',
                 '    switch (sel) {'] + prongs + ['        else => {},', '    }', '}']
        (d / 'main.zig').write_text('\n'.join(main) + '\n')
        r = subprocess.run([self.a.zig, 'build-exe', 'main.zig', '-O', 'Debug', '-lc', '--cache-dir', 'zc',
                            '--global-cache-dir', str(self.ctx.work / 'zig-global-cache'), '-femit-bin=run'],
                           cwd=d, capture_output=True, timeout=1800)
        if r.returncode != 0:
            if len(items) == 1:
                k = items[0][0]
                self.res[k] = (None, 'zig build-exe exit %s: %s' % (r.returncode, r.stderr.decode(errors='replace')[-300:]))
                return
            h = len(items) // 2  # bisect to attribute a compile failure to its unit
            self.zig_batch(items[:h])
            self.zig_batch(items[h:])
            return
        for j, (k, (src, name, n, vs)) in enumerate(items):
            p = subprocess.run([str(d / 'run')], env=dict(os.environ, ORACLE_UNIT=str(j)), capture_output=True, timeout=120)
            self.res[k] = self.collect((p.returncode, p.stderr, p.stdout), vs)


# ---------------------------------------------------------------- fixtures

def load_fixtures():
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
    for lang in LANGS[1:]:
        path = ROOT / ('tests/universal/translation/fixtures-f2-%s.json' % lang)
        if not path.is_file():
            info[lang] = 'absent'
            continue
        d = json.loads(path.read_text())
        info[lang] = {'path': str(path.relative_to(ROOT)), 'sha256': sha(path.read_bytes()),
                      'positives': len(d['positives']), 'controls': len(d.get('controls', [])),
                      'extra_positives': len(d.get('extra_positives', [])), 'refusals': len(d.get('refusals', []))}
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
        for x in d.get('extra_positives', []):
            b = x['source'].encode()
            if sha(b) != x['sha256']:
                raise Bad('%s extra %s sha256' % (lang, x['id']))
            out.append((lang, x['id'], x['id'], b, x['function'], x['inputs']))
        nip[lang] = d.get('not_in_profile', [])
    return out, nip, info


# ---------------------------------------------------------------- per fixture: U_A, retarget, native

def prepare(run, fx):
    lang, fid, base, src, name, n = fx
    row = {'fixture': fid, 'A': lang, 'checks': {}, 'failures': []}

    def fail(c, d):
        row['checks'][c] = 'FAIL'
        row['failures'].append({'check': c, 'detail': d})

    rc, env, err = run.imp(lang, src)
    try:
        if rc != 0:
            raise Bad('import_%s exit %s %s' % (lang, rc, err.decode(errors='replace')[:120]))
        sem, side = G.decode_envelope(env)
        m, ops = decode_member(sem)
        G.decode_sidecar(side, sem, len(ops), src)
        if m != lang or G.normalized_root(ops) is None or G.arity(ops) != n:
            raise Bad('member %s / NORMALIZED / arity %d' % (m, G.arity(ops)))
    except Bad as e:
        fail('import', str(e))
        row['result'] = 'FAIL'
        return row, None
    vs = G.vectors(n, base)
    ref = [G.evaluate(ops, v) for v in vs]
    Rs = {M: G.envelope(R(sem, M)) for M in LANGS}
    tool_R = {}
    for M in LANGS:  # compose-v2 section 2: the public retarget equals this file's R, byte for byte
        rcr, outr, _ = run.tool('retarget', env, M)
        tool_R[M] = outr if rcr == 0 else None
        if rcr == 'absent':
            row['checks']['retarget-%s' % M] = 'NOT_RUN'
        elif (rcr, outr) == (0, Rs[M]):
            row['checks']['retarget-%s' % M] = 'PASS'
        else:
            fail('retarget-%s' % M, {'status': rcr, 'stdout_sha256': sha(outr), 'R_sha256': sha(Rs[M])})
    if tool_R['go'] is not None:  # compose-v2 section 2: byte-deterministic
        rcd, outd, _ = run.tool('retarget', env, 'go', fresh=True)
        if (rcd, outd) == (0, tool_R['go']):
            row['checks']['retarget-deterministic'] = 'PASS'
        else:
            fail('retarget-deterministic', {'status': rcd})
    rcl, outl, _ = run.tool('lower', env)
    want_lower = 0 if lang == 'c' else 68 if lang in V1CORE else 65
    row['lower_direct_status'] = rcl
    if rcl == want_lower and (rcl == 0) == bool(outl):
        row['checks']['lower-direct-%s' % want_lower] = 'PASS'
    else:
        fail('lower-direct-%s' % want_lower, {'status': rcl, 'stdout_bytes': len(outl)})
    U_A = {'env': env, 'sem': sem, 'ops': ops, 'vs': vs, 'ref': ref, 'R': Rs, 'tool_R': tool_R,
           'oracle': run.oracle_key(lang, src, name, n, vs, base), 'native': run.native(N(sem), n, vs, base)}
    row.update({'semantic_identity': G.identity(sem), 'N_identity': G.identity(N(strip(sem, ops, lang))),
                'vectors': len(vs)})
    return row, U_A


# ---------------------------------------------------------------- one composition

def compose(run, fx, B, U_A):
    lang, fid, base, src, name, n = fx
    sem, ops, vs = U_A['sem'], U_A['ops'], U_A['vs']
    rec = {'fixture': fid, 'A': lang, 'B': B, 'composition': '%s->%s' % (lang, B), 'checks': {}, 'failures': []}

    def fail(c, d):
        rec['checks'][c] = 'FAIL'
        rec['failures'].append({'check': c, 'detail': d})

    def ok(c):
        rec['checks'].setdefault(c, 'PASS')

    exp = TOOLS[B][1]
    env_in, mode = U_A['env'], 'direct'
    if B in V1CORE and lang not in V1CORE:
        rc0, o0, _ = run.tool(exp, U_A['env'])
        (ok('v1-core-refuses-v2-65') if (rc0, o0) == (65, b'') else
         fail('v1-core-refuses-v2-65', {'status': rc0, 'stdout_bytes': len(o0)}))
        if U_A['tool_R'][B] is None:
            rec['result'] = 'NOT_RUN'
            rec['why'] = 'retarget absent or failed'
            return rec, None
        env_in, mode = U_A['tool_R'][B], 'retarget R(U_A, %s)' % B
    rec['export_input'] = mode
    rc, E, err = run.tool(exp, env_in)
    t = C1.root_tree(ops)
    bd = bounds(B, t, n, run)
    if bd['over']:
        rec['expect'] = 'LIMIT'
        (ok('export-limit-67') if (rc, E) == (67, b'') else fail('export-limit-67', {'status': rc, 'bounds': bd}))
        rec['result'] = 'FAIL' if rec['failures'] else 'PASS_LIMIT'
        return rec, None
    if rc != 0 or not E:
        fail('export', {'status': rc, 'stderr': err.decode(errors='replace')[:200], 'bounds': bd})
        rec['result'] = 'FAIL'
        return rec, None
    rec['src_B_sha256'] = sha(E)
    rc2, E2, _ = run.tool(exp, env_in, fresh=True)
    (ok('export-deterministic') if (rc2, E2) == (rc, E) else fail('export-deterministic', 'two runs differ'))
    so = G.envelope(G.decode_envelope(env_in)[0])
    rc3, E3, _ = run.tool(exp, so)
    (ok('4.3-semantic-only-identical') if (rc3, E3) == (0, E) else fail('4.3-semantic-only-identical', {'status': rc3}))
    for M in LANGS:
        want = (65, b'') if B in V1CORE and M not in V1CORE else (0, E)
        rcm, Em, _ = run.tool(exp, U_A['R'][M])
        (ok('4.3-R-%s' % M) if (rcm, Em) == want else fail('4.3-R-%s' % M, {'status': rcm, 'want': want[0]}))
    expected = render(B, t, n, run)
    (ok('export-form') if E.decode(errors='replace') == expected else
     fail('export-form', {'expected': expected, 'got': E.decode(errors='replace')}))
    rec['src_B_equals_source_bytes'] = E == src
    rc, envB, err = run.imp(B, E)
    if rc != 0:
        fail('import_B', {'status': rc, 'stderr': err.decode(errors='replace')[:200]})
    else:
        try:
            semB, sideB = G.decode_envelope(envB)
            mB, opsB = decode_member(semB)
            G.decode_sidecar(sideB, semB, len(opsB), E)
            if mB != B or G.normalized_root(opsB) is None:
                raise Bad('member %s / NORMALIZED' % mB)
            if N(strip(semB, opsB, B)) == N(strip(sem, ops, lang)):
                ok('4.1-identity')
            else:
                fail('4.1-identity', {'N_strip_U_A': G.identity(N(strip(sem, ops, lang))),
                                      'N_strip_U_B': G.identity(N(strip(semB, opsB, B)))})
            if B == lang:
                rec['same_language_semantic_bytes_equal'] = semB == sem
            m = next(({'vector': [str(x) for x in v], 'reference': str(w), 'got': str(G.evaluate(opsB, v))}
                      for v, w in zip(vs, U_A['ref']) if G.evaluate(opsB, v) != w), None)
            (fail('U_B-vs-reference', m) if m else ok('U_B-vs-reference'))
        except Bad as e:
            fail('import_B-decode', str(e))
    rec['_legs'] = {'oracle_A': U_A['oracle'], 'oracle_B': run.oracle_key(B, E, 'calc', n, vs, base)}
    return rec, E


def finalize(run, rec, U_A):
    if '_legs' not in rec:
        return
    legs = {'reference': 'PASS'}
    for leg, res in [(l, run.res[k]) for l, k in rec.pop('_legs').items()] + [('native', U_A['native'][:2])]:
        if res[0] is None:
            legs[leg] = 'FAIL'
            rec['checks']['4.2-' + leg] = 'FAIL'
            rec['failures'].append({'check': '4.2-' + leg, 'detail': res[1]})
            continue
        m = G.compare(U_A['ref'], res[0], U_A['vs'])
        legs[leg] = 'FAIL' if m else 'PASS'
        rec['checks']['4.2-' + leg] = legs[leg]
        if m:
            rec['failures'].append({'check': '4.2-' + leg, 'detail': m})
    rec['legs'] = legs
    rec['result'] = 'FAIL' if rec['failures'] else 'PASS'


# ---------------------------------------------------------------- mutants of exporter outputs

def mut_bare_ull(t):
    x = next((y for y in C1.preorder(t) if y['t'] == 'k'), None)
    if x:
        x['bare'] = True
        return t


NEW_MUTANTS = {'cpp': [m for m in C1.COMMON] + [('bare-ULL-literal', mut_bare_ull)],
               'zig': [m for m in C1.COMMON] + [('plain-operator', C1.mut_plain)],
               'go': [m for m in C1.COMMON]}


def mutants(run, ev, outputs):
    rows = []
    for (B, text), (ops, n, vs, ref, base) in outputs.items():
        t0 = C1.root_tree(ops)
        if B == 'c':
            muts = [(nm, fn(text)) for nm, fn in G.MUT.C_OPERATORS]
        elif B in ('rust', 'python'):
            t = C1.parse_rust(text) if B == 'rust' else C1.parse_python(text)
            muts = [(nm, C1.tree_text(B, m, n) if m is not None else None)
                    for nm, fn in C1.TREE_MUTANTS[B] for m in [fn(C1.clone(t))]]
        else:
            if text != render(B, t0, n, run):  # mutate the tree only where the output is its rendering
                rows.append({'B': B, 'operator': 'form-mismatch', 'killed': False})
                continue
            muts = [(nm, render(B, m, n, run) if m is not None else None)
                    for nm, fn in NEW_MUTANTS[B] for m in [fn(C1.clone(t0))]]
        semN = N(strip(G.build_sem(ops), ops, 'c'))
        for nm, mt in muts:
            if mt is None or mt == text:
                continue
            det = []
            if mt != render(B, t0, n, run):
                det.append('export-form')
            rc, envB, _ = run.imp(B, mt.encode())
            if rc != 0:
                det.append('import_B-refused-%s' % rc)
            else:
                try:
                    semB, _ = G.decode_envelope(envB)
                    mB, opsB = decode_member(semB)
                    if N(strip(semB, opsB, mB)) != semN:
                        det.append('4.1-identity')
                    if any(G.evaluate(opsB, v) != w for v, w in zip(vs, ref)):
                        det.append('U_B-vs-reference')
                except Bad:
                    det.append('import_B-decode')
            rows.append({'B': B, 'operator': '%s-%s' % (B, nm) if not nm.startswith(B + '-') else nm,
                         'mutant_sha256': sha(mt.encode()), 'detected_by': det,
                         '_oracle': run.oracle_key(B, mt.encode(), 'calc', n, vs, base + '-mut'), '_vs': vs, '_ref': ref})
    run.flush_zig()
    for r in rows:
        if '_oracle' in r:
            res = run.res[r.pop('_oracle')]
            vs, ref = r.pop('_vs'), r.pop('_ref')
            if res[0] is None:
                r['detected_by'].append('oracle_B-failed')
            elif G.compare(ref, res[0], vs):
                r['detected_by'].append('oracle_B-vs-reference')
            r['killed'] = bool(r['detected_by'])
            r['killed_beyond_form'] = bool([x for x in r['detected_by'] if x != 'export-form'])
    by = {}
    for r in rows:
        b = by.setdefault(r['operator'], {'total': 0, 'killed': 0, 'killed_beyond_form': 0})
        b['total'] += 1
        b['killed'] += r['killed']
        b['killed_beyond_form'] += r.get('killed_beyond_form', False)
    ev['mutants'] = {'total': len(rows), 'killed': sum(r['killed'] for r in rows),
                     'killed_beyond_form': sum(r.get('killed_beyond_form', False) for r in rows),
                     'by_operator': by, 'rows': rows}


# ---------------------------------------------------------------- refusals

def refusals(run, ev, base_env, base_ops):
    rows = []

    def expect(tool, label, data, args, want):
        rc, out, err = run.tool(tool, data, *args)
        rows.append({'tool': tool, 'case': label, 'want': want, 'status': rc, 'stdout_bytes': len(out),
                     'stderr': err.decode(errors='replace')[:100],
                     'result': 'NOT_RUN' if rc == 'absent' else 'PASS' if rc == want and out == b'' else 'FAIL'})

    rec = lambda lang, op, t, attr, eff, alias, obl, A=U, B=U, imm=0: struct.pack(
        '<8BHHQ', lang, 1, op, t, attr, eff, alias, obl, A, B, imm)
    dig = G.SEMANTICS_DIGEST

    def unit(ver, lang, prof, rt, abi, recs):
        return G.envelope(b'OMSA' + bytes([ver, lang, prof, 1, rt, abi]) + struct.pack('<H', len(recs)) + dig + b''.join(recs))

    units = [('cpython-bigint-profile-3', unit(1, 3, 3, 1, 1, [rec(3, 9, 2, 0, 0, 0, 1), rec(3, 9, 2, 0, 0, 0, 1, imm=1),
                                                             rec(3, 2, 2, 2, 20, 255, 8, 0, 1)]), 1),
             ('osc-profile-5', G.envelope(G.build_sem(base_ops, lang=4, prof=5)), 1),
             ('python-dynamic-profile-6', unit(1, 3, 6, 1, 2, [rec(3, 9, 1, 0, 0, 0, 1), rec(3, 9, 1, 0, 0, 0, 1, imm=1),
                                                             rec(3, 2, 1, 1, 0, 0, 5, 0, 1)]), 1)]
    for m, (ver, lang, prof, dialect, arith) in MEMBERS.items():
        units.append(('%s-member-division' % m, unit(ver, lang, prof, 0, 1, [
            rec(dialect, 9, 1, 0, 0, 0, 1), rec(dialect, 9, 1, 0, 0, 0, 1, imm=1), rec(dialect, 5, 1, 1, 0, 0, arith | 2, 0, 1),
            rec(dialect, 1, 1, 1, 0, 0, 0), rec(dialect, 2, 1, 1, 0, 0, arith, 2, 3)]), ver))
    for label, e, ver in units:
        rc, out, _ = G.run([run.a.sa2], b'\x02' + e)
        if (rc, out) != (0, e):
            rows.append({'tool': 'sa-v2', 'case': label, 'status': rc, 'result': 'FAIL',
                         'detail': 'probe is not a valid source-aware v2 unit, so it cannot test 68'})
            continue
        for B in LANGS:  # compose-v2 sections 1 and 3: family tools 68; v1-core tools see version 2 as 65
            expect(TOOLS[B][1], 'outside-family ' + label, e, [], 65 if B in V1CORE and ver == 2 else 68)
        expect('lower', 'outside-family ' + label, e, [], 65 if ver == 2 else 68)
        expect('retarget', 'outside-family ' + label, e, ['c'], 68)
    for label, bad in G.malformed_envelopes(base_env):
        for tool in ('cpp-export', 'zig-export', 'go-export', 'retarget'):
            expect(tool, 'malformed ' + label, bad, ['c'] if tool == 'retarget' else [], 65)
    for tool in ('cpp-export', 'zig-export', 'go-export'):
        expect(tool, 'any argument', base_env, ['calc'], 64)
    for args in ([], ['java'], ['c', 'go']):
        expect('retarget', 'bad command %r' % args, base_env, args, 64)
    for lang in LANGS[1:]:
        for rid, src, want, sha_ok in C1.lane_refusals(lang):
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
    ap.add_argument('--repo', default=str(ROOT))
    ap.add_argument('--omc', required=True)
    ap.add_argument('--bridge-native', required=True)
    ap.add_argument('--bridge-repo', default=os.environ.get('OMEGA_BRIDGE_REPO'),
                    help='omega-bridge clone holding %s (default $OMEGA_BRIDGE_REPO; no host default)' % G.BRIDGE_COMMIT)
    ap.add_argument('--sa2', required=True, help='v2 codec binary build/L832/sa (refusal probes must be valid v2)')
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    ap.add_argument('--rustc', default=os.environ.get('RUSTC', 'rustc'))
    ap.add_argument('--python', default=sys.executable)
    # oracles come from the caller ($ZIG, $GO, $GOFMT) or PATH; a missing one is NOT_RUN, never a host default
    ap.add_argument('--zig', default=os.environ.get('ZIG', 'zig'))
    ap.add_argument('--go', default=os.environ.get('GO', 'go'))
    ap.add_argument('--gofmt', default=os.environ.get('GOFMT', 'gofmt'))
    ap.add_argument('--no-mutants', action='store_true')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    (out / 'work').mkdir()
    repo = Path(a.repo).resolve()
    ev = {'schema': 'omega.universal.translation.validate.compose.v2', 'contract': {'path': COMPOSE2[0], 'sha256': COMPOSE2[1]},
          'validator_sha256': sha(Path(__file__).read_bytes()),
          'validate_compose_sha256': sha((HERE / 'validate_compose.py').read_bytes()),
          'validate_g1_sha256': sha((HERE / 'validate_g1.py').read_bytes()),
          'evidence_class': 'REFERENCE + DIFFERENTIAL, NATIVE (host AArch64); no PROOF'}

    def not_run(why):
        ev['result'] = 'NOT_RUN'
        ev['reason'] = why
        (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
        print('NOT_RUN', why)
        return 3

    pins = {}
    for rel, want in [COMPOSE2, SA2, C1.COMPOSE, G.CONTRACT] + list(G.FROZEN.items()):
        p = ROOT / rel
        got = sha(p.read_bytes()) if p.is_file() else None
        if got != want:
            return not_run('%s sha256 %s, pinned %s' % (rel, got, want))
        pins[rel] = got
    for rel in LANE_SPECS.values():
        pins[rel] = sha((ROOT / rel).read_bytes()) if (ROOT / rel).is_file() else None
    ev['pins'] = pins
    found = {}
    for key, exe in (('cc', a.cc), ('cxx', a.cxx), ('rustc', a.rustc), ('python', a.python), ('zig', a.zig),
                     ('go', a.go), ('gofmt', a.gofmt)):
        found[key] = shutil.which(exe)
        if found[key] is None:
            return not_run('oracle %s absent: %r (set it explicitly or put it on PATH)' % (key, exe))
    a.cc, a.cxx, a.rustc, a.python, a.zig, a.go = (found[k] for k in ('cc', 'cxx', 'rustc', 'python', 'zig', 'go'))
    gofmt = found['gofmt']
    if not a.bridge_repo:
        return not_run('no omega-bridge clone: pass --bridge-repo or set OMEGA_BRIDGE_REPO')
    for exe in (a.omc, a.bridge_native, a.sa2):
        if not Path(exe).is_file():
            return not_run('absent: %s' % exe)
    if sha(Path(a.omc).read_bytes()) != G.OMC_SHA:
        return not_run('omc is not the translation-v1 section 1 pin')
    rc, nsrc, _ = G.run(['git', '-C', a.bridge_repo, 'show', G.BRIDGE_COMMIT + ':src/native.om'])
    if rc != 0 or G.run([a.omc], nsrc, timeout=120)[1] != Path(a.bridge_native).read_bytes():
        return not_run('bridge generator is not omc(native.om @ %s)' % G.BRIDGE_COMMIT)
    tools, tinfo = {}, {}
    for name in ['lower', 'retarget'] + [t for p in TOOLS.values() for t in p]:
        p = repo / 'tools/adapters' / name
        if p.is_file() and os.access(p, os.X_OK):
            tools[name] = str(p)
            tinfo[name] = {'sha256': sha(p.read_bytes())}
            for bj in sorted(repo.glob('build/**/build.json')):
                if (bj.parent / name).is_file():
                    tinfo[name].update({'build': str(bj.parent.relative_to(repo)), 'binary_sha256': sha((bj.parent / name).read_bytes())})
        else:
            tinfo[name] = 'ABSENT'
    head = G.run(['git', '-C', repo, 'rev-parse', 'HEAD'])
    ev['repo'] = {'path': str(repo), 'head': head[1].decode().strip() if head[0] == 0 else None}
    ev['tools'] = tinfo
    ev['toolchains'] = {k: {'path': v[0], 'version': G.run(v)[1].decode().split('\n')[0], 'sha256': sha(Path(v[0]).read_bytes())}
                        for k, v in {'cc': [a.cc, '--version'], 'g++': [a.cxx, '--version'], 'rustc': [a.rustc, '--version'],
                                     'python': [a.python, '--version'], 'zig': [a.zig, 'version'], 'go': [a.go, 'version'],
                                     'gofmt': [gofmt, '-h']}.items()}
    ev['sa2_sha256'] = sha(Path(a.sa2).read_bytes())
    if 'lower' not in tools:
        return not_run('tools/adapters/lower absent')
    try:
        fixtures, nip, finfo = load_fixtures()
    except Bad as e:
        return not_run('fixtures: %s' % e)
    ev['fixtures'] = finfo
    ev['not_in_profile'] = nip
    args = SimpleNamespace(omc=a.omc, bridge_native=a.bridge_native, cc=a.cc, cxx=a.cxx, rustc=a.rustc, python=a.python,
                           zig=a.zig, go=a.go, gofmt=gofmt, sa2=a.sa2, tools=tools)
    run = Run(args, out)

    units, frows = [], []
    for fx in fixtures:
        if TOOLS[fx[0]][0] not in tools:
            frows.append({'fixture': fx[1], 'A': fx[0], 'result': 'NOT_RUN'})
            units.append((fx, None))
            continue
        row, U_A = prepare(run, fx)
        row.setdefault('result', 'FAIL' if row['failures'] else 'PASS')
        frows.append(row)
        units.append((fx, U_A))
    byid = {fx[1]: u for fx, u in units}
    fid_rows = []
    for fx, u in units:
        if fx[0] == 'c' or u is None or not fx[1].startswith('F2-') or fx[2] not in byid or byid[fx[2]] is None:
            continue
        c = byid[fx[2]]
        same = all(G.evaluate(u['ops'], v) == G.evaluate(c['ops'], v) for v in u['vs'])
        fid_rows.append({'fixture': fx[1], 'f1': fx[2], 'values_equal': same,
                         'N_strip_equal_to_C_import': N(strip(u['sem'], u['ops'], fx[0])) == N(strip(c['sem'], c['ops'], 'c')),
                         'result': 'PASS' if same else 'FAIL'})
    ev['f2_fidelity'] = {'total': len(fid_rows), 'pass': sum(r['result'] == 'PASS' for r in fid_rows),
                         'N_equal': sum(r['N_strip_equal_to_C_import'] for r in fid_rows), 'rows': fid_rows}
    ev['fixture_rows'] = frows

    comps, outputs, pairs = [], {}, []
    for fx, U_A in units:
        for B in LANGS:
            key = '%s->%s' % (fx[0], B)
            if U_A is None or TOOLS[B][1] not in tools or TOOLS[B][0] not in tools:
                failed = any(r['fixture'] == fx[1] and r.get('result') == 'FAIL' for r in frows)
                comps.append({'fixture': fx[1], 'A': fx[0], 'B': B, 'composition': key, 'result': 'FAIL' if failed else 'NOT_RUN'})
                continue
            rec, E = compose(run, fx, B, U_A)
            comps.append(rec)
            pairs.append((rec, U_A))
            if E is not None:
                outputs.setdefault((B, E.decode(errors='replace')), (U_A['ops'], fx[5], U_A['vs'], U_A['ref'], fx[2]))
        print(fx[1], flush=True)
    run.flush_zig()
    for rec, U_A in pairs:
        finalize(run, rec, U_A)
    summary = {}
    for c in comps:
        s = summary.setdefault(c['composition'], {'PASS': 0, 'PASS_LIMIT': 0, 'FAIL': 0, 'NOT_RUN': 0, 'export_input': set()})
        s[c['result']] += 1
        if 'export_input' in c:
            s['export_input'].add(c['export_input'])
    for s in summary.values():
        s['export_input'] = sorted(s['export_input'])
    order = ['%s->%s' % (x, y) for x in LANGS for y in LANGS]
    ev['compositions'] = {k: summary[k] for k in order if k in summary and k.split('->')[0] != k.split('->')[1]}
    ev['same_language_round_trips'] = {k: summary[k] for k in order if k in summary and k.split('->')[0] == k.split('->')[1]}
    per = {}
    for c in comps:
        for chk, r in c.get('checks', {}).items():
            per.setdefault(c['composition'], {}).setdefault(chk, {}).setdefault(r, 0)
            per[c['composition']][chk][r] += 1
    ev['per_check'] = per
    base = next((u for fx, u in units if fx[1] == 'F1-P05'), None)
    if base:
        refusals(run, ev, base['env'], base['ops'])
    if not a.no_mutants:
        mutants(run, ev, outputs)
    ev['cases'] = comps
    failed = ['%s %s' % (c['composition'], c['fixture']) for c in comps if c['result'] == 'FAIL']
    failed += ['fixture %s %s' % (r['fixture'], [f['check'] for f in r.get('failures', [])]) for r in frows if r.get('result') == 'FAIL']
    failed += ['f2-fidelity %s' % r['fixture'] for r in fid_rows if r['result'] != 'PASS']
    failed += ['refusal %s %s' % (r['tool'], r['case']) for r in ev.get('refusals', {}).get('rows', []) if r['result'] == 'FAIL']
    if 'mutants' in ev and ev['mutants']['killed'] != ev['mutants']['total']:
        failed.append('mutants survived: %d' % (ev['mutants']['total'] - ev['mutants']['killed']))
    if not base:
        failed.append('F1-P05 did not import: refusal probes not run')
    notrun = [c for c in comps if c['result'] == 'NOT_RUN'] + [r for r in ev.get('refusals', {}).get('rows', []) if r['result'] == 'NOT_RUN']
    notrun += [1 for r in frows for v in r.get('checks', {}).values() if v == 'NOT_RUN']
    ev['not_run'] = len(notrun)
    ev['failed'] = failed
    ev['result'] = 'FAIL' if failed else 'NOT_RUN' if notrun else 'PASS'
    (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
    retarget_checks = sum(1 for r in frows for k, v in r.get('checks', {}).items() if k.startswith('retarget-') and v == 'PASS')
    print(json.dumps({'result': ev['result'],
                      'compositions': {k: {x: y for x, y in v.items() if y} for k, v in ev['compositions'].items()},
                      'same_language_round_trips': {k: {x: y for x, y in v.items() if y} for k, v in ev['same_language_round_trips'].items()},
                      'retarget_equals_R': retarget_checks,
                      'f2_fidelity': '%d/%d (N equal %d)' % (ev['f2_fidelity']['pass'], ev['f2_fidelity']['total'], ev['f2_fidelity']['N_equal']),
                      'refusals': {k: ev['refusals'][k] for k in ('total', 'pass', 'fail', 'not_run')} if 'refusals' in ev else 'NOT_RUN',
                      'mutants': '%d/%d killed (%d beyond the form check)' % (ev['mutants']['killed'], ev['mutants']['total'], ev['mutants']['killed_beyond_form']) if 'mutants' in ev else 'NOT_RUN',
                      'failed': failed[:40]}, indent=1))
    return 1 if failed else 3 if notrun else 0


if __name__ == '__main__':
    raise SystemExit(main())
