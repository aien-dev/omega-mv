#!/usr/bin/env python3
"""S2 author self-test (spec/universal/translation-s2-v1.md section 6): signed i64, comparisons, select.

Three directions, each exercised through its public command and checked against external oracles:
  c-import   tools/adapters/c-i64-import; every unit is evaluated by this file's bigint reference and
             compared with the C source compiled by gcc -O0 -fsanitize=signed-integer-overflow
             -fno-sanitize-recover=all (one fork per vector; a sanitizer exit is the error outcome)
  rust       tools/adapters/rust-i64-export; rustc build-setting matrix (section 6): D debug (overflow-checks
             on, panic = error), R release (opt-level 3, overflow-checks off: attribute 6 overflow wraps, a
             REFINEMENT of the C error), RC release with overflow-checks on (panic = error)
  native     tools/adapters/lower-s2 -> pinned omc 734f0a39 (function mode) -> pinned omega-bridge generator
             -> C caller (ABI v1: an i64 travels as its uint64_t two's-complement bits); status 1 = error
plus the v3 codec (v1/v2 decode byte-identical with the v2 codec build/L832/sa over a mutated corpus, v3
positive and refusal rows), the composition C -> Omega -> Rust (rustc output equal to the C oracle on every
vector), and tool-source mutants (>= 8 per direction, each recompiled with its pinned compiler, all killed).
Exit: 0 PASS, 1 FAIL, 3 NOT_RUN (an oracle or pin is absent; nothing is passed).
"""
from pathlib import Path
import argparse, hashlib, itertools, json, os, random, struct, subprocess, sys, tempfile, time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE))
import validate_g1 as V  # noqa: E402  (process runner, vector packing and the native caller template)

MASK = (1 << 64) - 1
MIN, MAX = -(1 << 63), (1 << 63) - 1
U = 65535
SEED_OMC = '191b776ef98821597c3e30655da6d680a9b66edbc9ceaca3c39c61ea6df098e9'
NATIVE_OMC = '734f0a39319fbfa930bcc62cb30dbf06d47d78e126372b74f5baf478e6126b61'
BOUNDARY = [MIN, MIN + 1, -3037000500, -2, -1, 0, 1, 2, 3037000499, 3037000500, 1 << 32, MAX - 1, MAX]
GCC_ORACLE = ['-std=c17', '-O0', '-fsanitize=signed-integer-overflow', '-fno-sanitize-recover=all',
              '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter']
RUSTC = ['--edition', '2021', '-C', 'opt-level=0', '-C', 'debug-assertions=on', '-C', 'overflow-checks=on',
         '-A', 'warnings']
# Build-setting matrix (translation-s2-v1 section 6). R spells rustc's release defaults explicitly so the
# default is never assumed.
RUST_SETTINGS = {
    'D': RUSTC,
    'R': ['--edition', '2021', '-C', 'opt-level=3', '-C', 'debug-assertions=off', '-C', 'overflow-checks=off',
          '-A', 'warnings'],
    'RC': ['--edition', '2021', '-C', 'opt-level=3', '-C', 'overflow-checks=on', '-A', 'warnings'],
}
MATRIX = ('D', 'R', 'RC')


class NotRun(Exception):
    pass


def s64(x):
    x &= MASK
    return x - (1 << 64) if x >> 63 else x


def sha(b):
    return hashlib.sha256(b).hexdigest()


# ------------------------------------------------------------------ reference decoder and evaluator
def records(env):
    """OMSA records of a structurally valid envelope (the codec under test has already accepted it)."""
    sn = struct.unpack_from('<I', env, 5)[0]
    sem = env[13:13 + sn]
    n = struct.unpack_from('<H', sem, 10)[0]
    out = []
    for i in range(n):
        d, ver, op, t, at, ef, al, ob, a, b, imm = struct.unpack_from('<8BHHQ', sem, 44 + 20 * i)
        out.append(dict(op=op, t=t, at=at, ob=ob, a=a, b=b, imm=imm))
    return {'ver': sem[4], 'lang': sem[5], 'prof': sem[6], 'rt': sem[8], 'abi': sem[9]}, out


class Trap(Exception):
    pass


def evaluate(ops, args, cov=None, wrap6=False):
    """Demand-driven bigint evaluation (translation-s2-v1 section 3, amendment A2): select evaluates its
    condition and only the chosen arm; attribute 6 i64 overflow is the error outcome, or with wrap6 (the
    rustc overflow-checks=off build) the result reduced mod 2^64. -> (status, bits)."""
    memo = {}

    def ev(k):
        if k in memo:
            return memo[k]
        r = ops[k]
        op, t = r['op'], r['t']
        if op == 9:
            x = args[r['imm']]
            v = s64(x) if t == 5 else x & MASK
        elif op == 1:
            v = s64(r['imm']) if t == 5 else r['imm']
        elif op in (2, 3, 4):
            a, b = ev(r['a']), ev(r['b'])
            e = a + b if op == 2 else a - b if op == 3 else a * b
            if t == 1:
                v = e & MASK
            elif r['at'] == 5:
                v = s64(e)
            else:
                if not MIN <= e <= MAX:
                    if not wrap6:
                        raise Trap()
                    e = s64(e)
                v = e
        elif 10 <= op <= 15:
            a, b = ev(r['a']), ev(r['b'])
            v = int([a == b, a != b, a < b, a <= b, a > b, a >= b][op - 10])
            if cov is not None:
                cov.add((k, v))
        elif op == 16:
            c = ev(r['a'])
            if cov is not None:
                cov.add((k, c))
            v = ev(r['b']) if c else ev(r['imm'])
        elif op == 17:
            v = 1 - ev(r['a'])
        else:
            raise AssertionError('op %d' % op)
        memo[k] = v
        return v

    try:
        return (0, ev(len(ops) - 1) & MASK)
    except Trap:
        return (1, None)


def same(ref, got):
    return ref[0] == got[0] and (ref[0] != 0 or ref[1] == got[1])


def vectors(n, tag, extra=()):
    if n == 0:
        return [()]
    bits = [v & MASK for v in BOUNDARY]
    if len(bits) ** n <= 2200:
        out = list(itertools.product(bits, repeat=n))
    else:
        out = [tuple([v] * n) for v in bits]
        for pos in range(n):
            for base in bits:
                for v in bits:
                    t = [base] * n
                    t[pos] = v
                    out.append(tuple(t))
        rs = random.Random('S2-boundary-' + tag)
        out += [tuple(rs.choice(bits) for _ in range(n)) for _ in range(600)]
    rr = random.Random('S2-random-' + tag)
    out += [tuple(rr.getrandbits(64) for _ in range(n)) for _ in range(64)]
    out += [tuple(rr.randint(-40, 40) & MASK for _ in range(n)) for _ in range(64)]
    out += [tuple(v & MASK for v in e) for e in extra]
    seen, uniq = set(), []
    for v in out:
        if v not in seen:
            seen.add(v)
            uniq.append(v)
    return uniq


# ------------------------------------------------------------------ fixtures (spec section 6 classes)
PARAMS = 'abcdef'


def csrc(n, body):
    ps = ', '.join('int64_t %s' % PARAMS[i] for i in range(n)) or 'void'
    return '#include <stdint.h>\nint64_t calc(%s) { return %s; }\n' % (ps, body)


W = '(int64_t)((uint64_t)%s %s (uint64_t)%s)'
C_POS = [  # (id, n, body, explicit at-boundary and one-past vectors)
    ('add', 2, 'a + b', [(MAX, 0), (MAX, 1), (MIN, 0), (MIN, -1), (MAX, MIN)]),
    ('sub', 2, 'a - b', [(MIN, 0), (MIN, 1), (MAX, -1), (MAX, 0), (-1, MAX), (-2, MAX)]),
    ('mul', 2, 'a * b', [(MIN, -1), (MIN, 1), (3037000499, 3037000499), (3037000500, 3037000500),
                         (-3037000499, 3037000499), (4294967296, 2147483648), (4294967296, 2147483647)]),
    ('wadd', 2, W % ('a', '+', 'b'), [(MAX, 1), (MIN, -1)]),
    ('wsub', 2, W % ('a', '-', 'b'), [(MIN, 1), (MAX, -1)]),
    ('wmul', 2, W % ('a', '*', 'b'), [(MIN, -1), (3037000500, 3037000500)]),
    ('eq', 2, 'a == b ? a : b - 1LL', [(MIN, MIN), (MIN, MIN + 1)]),
    ('ne', 2, 'a != b ? a - b : 7LL', [(5, 5), (0, MIN), (-1, MAX)]),
    ('lt', 2, 'a < b ? b - a : a - b', [(MIN, MAX), (MAX, MIN), (-1, MAX), (0, MIN)]),
    ('le', 2, 'a <= b ? a : b', [(MAX, MAX), (MIN, MIN)]),
    ('gt', 2, 'a > b ? a * 2LL : b', [(4611686018427387903, 0), (4611686018427387904, 0)]),
    ('ge', 2, 'a >= b ? 1LL : 0LL', [(MIN, MAX), (MAX, MIN)]),
    ('max3', 3, 'a < b ? (b < c ? c : b) : a < c ? c : a', []),
    ('guard', 1, 'a < 9223372036854775807LL ? a + 1LL : a', [(MAX,), (MAX - 1,)]),
    ('guardmul', 2, 'b == 0LL ? 0LL : a > 3037000499LL ? 1LL : a < 0LL - 3037000499LL ? 2LL : b > 3037000499LL '
                    '? 3LL : b < 0LL - 3037000499LL ? 4LL : a * b', []),
    ('minlit', 1, 'a - 9223372036854775807LL - 1LL', [(0,), (-1,), (MAX,)]),
    ('mixwrap', 2, W % ('a', '*', '9223372036854775807LL') + ' + b', [(1, 1), (2, 0), (2, MIN)]),
    ('ovfcheck', 2, W % ('a', '+', 'b') + ' < a ? 1LL : 0LL', [(MAX, 1), (1, -1)]),
    ('condovf', 3, 'a * b > c ? a : c', [(MIN, -1, 0), (MAX, 2, 0)]),
    ('prec', 2, '(a + b) * (a - b) >= 0LL ? a * a - b * b : 0LL', []),
    ('six', 6, 'a + b + c + d + e + f', [(MAX, 1, 0, 0, 0, -1), (MAX, 0, 0, 0, 0, 1)]),
    ('six-sel', 6, 'a + b > c - d ? e * f : a - f', [(MAX, 1, 0, 0, 2, 3), (0, 0, 0, 0, MAX, 2), (MIN, 0, 0, 0, 0, 1)]),
    ('ident', 1, 'a', []),
    ('neg', 1, '0LL - a', [(MIN,), (MIN + 1,)]),
    ('const', 0, '5LL', []),
    ('nestwrap', 2, '(int64_t)((uint64_t)(a < b ? a : b) - (uint64_t)(a * 2LL))', [(MIN, 0), (4611686018427387904, 0)]),
]

C_REFUSE = [  # (id, source text, expected exit)
    ('u64-param', '#include <stdint.h>\nint64_t calc(uint64_t a) { return a; }\n', 66),
    ('u64-return', '#include <stdint.h>\nuint64_t calc(int64_t a) { return a; }\n', 66),
    ('int-param', '#include <stdint.h>\nint64_t calc(int a) { return a; }\n', 66),
    ('no-suffix', csrc(1, 'a + 1'), 68),
    ('ull-suffix', csrc(1, 'a + 1ULL'), 68),
    ('lit-2p63', csrc(1, 'a + 9223372036854775808LL'), 67),
    ('octal', csrc(1, 'a + 01LL'), 68),
    ('unary-minus', csrc(1, '-a'), 68),
    ('logical-not', csrc(2, '!a'), 68),
    ('div', csrc(2, 'a / b'), 68),
    ('rem', csrc(2, 'a % b'), 68),
    ('shift', csrc(2, 'a << b'), 68),
    ('cmp-value', csrc(2, 'a < b'), 68),
    ('cmp-in-arith', csrc(2, '(a < b) + 1LL'), 68),
    ('cond-not-cmp', csrc(3, 'a ? b : c'), 68),
    ('chained-cmp', csrc(3, 'a < b < c ? a : b'), 68),
    ('plain-cast', csrc(1, '(int64_t)a'), 68),
    ('wrap-div', csrc(2, W % ('a', '/', 'b')), 68),
    ('wrap-u64-result', csrc(2, '(uint64_t)((uint64_t)a + (uint64_t)b)'), 68),
    ('wrap-half', csrc(2, '(int64_t)((uint64_t)a + b)'), 68),
    ('assign', csrc(2, 'a = b'), 69),
    ('spaced-le', csrc(2, 'a < = b ? a : b'), 68),
    ('logical-and', csrc(2, 'a && b'), 68),
    ('missing-semi', '#include <stdint.h>\nint64_t calc(int64_t a) { return a }\n', 65),
    ('missing-colon', csrc(2, 'a < b ? a b'), 65),
    ('nul', '#include <stdint.h>\nint64_t calc(int64_t a) { return a;\0 }\n', 65),
    ('backslash', '#include <stdint.h>\nint64_t calc(int64_t a) { return \\\na; }\n', 72),
    ('no-header', 'int64_t calc(int64_t a) { return a; }\n', 72),
    ('seven-params', '#include <stdint.h>\nint64_t calc(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, '
                     'int64_t f, int64_t g) { return a; }\n', 67),
    ('deep', csrc(1, '(' * 18 + 'a' + ')' * 18), 67),
    ('dup-param', '#include <stdint.h>\nint64_t calc(int64_t a, int64_t a) { return a; }\n', 71),
    ('bad-name', '#include <stdint.h>\nint64_t calc(int64_t uint64_t) { return 1LL; }\n', 71),
    ('statement', '#include <stdint.h>\nint64_t calc(int64_t a) { if (a) return a; }\n', 69),
]


def sem(ops, lang=1, prof=10, rt=0, abi=1, ver=3):
    recs = b''
    for r in ops:
        recs += struct.pack('<8BHHQ', r.get('d', lang), r.get('v', 1), r['op'], r['t'], r['at'], r.get('ef', 0),
                            r.get('al', 0), r['ob'], r['a'], r['b'], r['imm'])
    return b'OMSA' + bytes([ver, lang, prof, 1, rt, abi]) + struct.pack('<H', len(ops)) + V.SEMANTICS_DIGEST + recs


def env_of(s, side=b''):
    return b'OMSB\x01' + struct.pack('<II', len(s), len(side)) + s + side


def R(op, t, at, ob, a=U, b=U, imm=0, **kw):
    d = dict(op=op, t=t, at=at, ob=ob, a=a, b=b, imm=imm)
    d.update(kw)
    return d


def IN(s, t=5):
    return R(9, t, 0, 1, imm=s)


def K(v, t=5):
    return R(1, t, 1 if t == 1 else 0, 0, imm=v & MASK)


def AR(op, a, b, at=6, t=5):
    return R(op, t, 1 if t == 1 else at, 1 if t == 1 or at == 5 else 33, a, b)


def CMP(op, a, b):
    return R(op, 6, 0, 0, a, b)


def SEL(c, x, y, t=5):
    return R(16, t, 0, 0, c, x, y)


def NOT(a):
    return R(17, 6, 0, 0, a)


HAND = [  # (id, ops, lang, prof): units no C source produces (u64, bool results, not, negative constants)
    ('not-bool', [IN(0), IN(1), CMP(12, 0, 1), NOT(2)], 1, 10),
    ('u64-mix', [IN(0, 1), IN(1, 1), CMP(12, 0, 1), AR(3, 1, 0, t=1), AR(3, 0, 1, t=1), SEL(2, 3, 4, t=1)], 2, 11),
    ('negconst', [IN(0), K(MIN), K(-5), CMP(13, 0, 2), AR(2, 0, 1, 5), SEL(3, 1, 4)], 3, 12),
    ('sel-not', [IN(0), IN(1), CMP(10, 0, 1), NOT(2), AR(4, 0, 1), SEL(3, 4, 0)], 5, 13),
    ('bool-const', [IN(0), K(1, 6), K(0, 6), AR(2, 0, 0), SEL(2, 3, 0), SEL(1, 4, 3)], 6, 14),
    ('shared-ovf', [IN(0), IN(1), AR(2, 0, 1), CMP(14, 0, 1), SEL(3, 2, 1), SEL(3, 1, 2), AR(3, 4, 5)], 7, 15),
    ('bool-sel', [IN(0), IN(1), CMP(12, 0, 1), CMP(15, 0, 1), SEL(2, 3, 2, t=6), NOT(4)], 1, 10),
    ('u64-cmp-i64', [IN(0, 1), IN(1), K(9, 1), CMP(14, 0, 2), AR(4, 1, 1), SEL(3, 4, 1)], 1, 10),
]

RUST_GOLDEN = {
    'add': 'pub fn calc(i0: i64, i1: i64) -> i64 { i0 + i1 }\n',
    'wmul': 'pub fn calc(i0: i64, i1: i64) -> i64 { i0.wrapping_mul(i1) }\n',
    'lt': 'pub fn calc(i0: i64, i1: i64) -> i64 { if i0 < i1 { i1 - i0 } else { i0 - i1 } }\n',
    'const': 'pub fn calc() -> i64 { 5i64 }\n',
    'not-bool': 'pub fn calc(i0: i64, i1: i64) -> bool { !(i0 < i1) }\n',
    'negconst': 'pub fn calc(i0: i64) -> i64 { if i0 <= (-5i64) { -9223372036854775808i64 } else { i0.wrapping_add('
                '(-9223372036854775808i64)) } }\n',
}


def arity(ops):
    return sum(1 for r in ops if r['op'] == 9)


def ptypes(ops):
    t = {}
    for r in ops:
        if r['op'] == 9:
            t[r['imm']] = r['t']
    return [t[i] for i in range(len(t))]


# ------------------------------------------------------------------ runners
class Ctx:
    def __init__(self, a, work):
        self.a, self.work, self.n = a, work, 0
        self.oracle_cache = {}

    def tmp(self, label):
        self.n += 1
        d = self.work / ('%04d-%s' % (self.n, label))
        d.mkdir(parents=True)
        return d

    def compile_run(self, cmd, d, vs, n):
        rc, out, err = V.run(cmd, timeout=300)
        if rc != 0:
            return None, 'compile exit %s: %s' % (rc, err.decode(errors='replace')[:400])
        rc, out, err = V.run([d / 'run'], V.pack_vectors(vs, n), timeout=300)
        if rc != 0:
            return None, 'run exit %s' % rc
        rows = out.decode().split('\n')[:-1]
        if len(rows) != len(vs):
            return None, 'row count %d != %d' % (len(rows), len(vs))
        return [tuple(int(x) for x in r.split()) for r in rows], None

    def c_oracle(self, src, n, vs):
        key = (src, tuple(vs))
        if key in self.oracle_cache:
            return self.oracle_cache[key]
        d = self.tmp('c-oracle')
        (d / 'unit.c').write_text(src)
        ps = ', '.join(['int64_t'] * n) or 'void'
        args = ', '.join('(int64_t)v[%d]' % i for i in range(n))
        (d / 'driver.c').write_text(ORACLE_DRIVER % {'ps': ps, 'k': n, 'args': args})
        res = self.compile_run([self.a.cc] + GCC_ORACLE + [d / 'unit.c', d / 'driver.c', '-o', d / 'run'], d, vs, n)
        self.oracle_cache[key] = res
        return res

    def rust_run(self, rs_text, types, rtype, vs, setting='D'):
        d = self.tmp('rust-' + setting)
        n = len(types)
        args = ', '.join(('v[%d] as i64' if t == 5 else 'v[%d]') % i for i, t in enumerate(types))
        bits = {1: 'x', 5: 'x as u64', 6: 'x as u64'}[rtype]
        (d / 'unit.rs').write_text(rs_text + RUST_DRIVER % {'k': n, 'args': args, 'bits': bits})
        return self.compile_run([self.a.rustc] + RUST_SETTINGS[setting] + [d / 'unit.rs', '-o', d / 'run'], d, vs, n)

    def native(self, om_text, n, vs, sym='omega_calc'):
        d = self.tmp('native')
        om = om_text.encode() if isinstance(om_text, str) else om_text
        (d / 'unit.om').write_bytes(om)
        rc, binary, err = V.run([self.a.omc], om, timeout=120)
        if rc != 0 or not binary:
            return None, 'omc exit %s: %s' % (rc, err.decode(errors='replace')[:300])
        frame = om + b'\0' + b'%08d' % len(binary) + binary
        rc, binding, err = V.run([self.a.bridge_native], frame, timeout=120)
        if rc != 0 or not binding:
            return None, 'bridge generator exit %s' % rc
        (d / 'binding.c').write_bytes(binding)
        (d / 'caller.c').write_text(V.native_driver(sym, n))
        return self.compile_run([self.a.cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', d / 'binding.c',
                                 d / 'caller.c', '-o', d / 'run'], d, vs, n)


ORACLE_DRIVER = r'''#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
int64_t calc(%(ps)s);
int main(void)
{
    uint64_t n, v[6] = {0};
    if (fread(&n, 8, 1, stdin) != 1)
        return 2;
    for (uint64_t j = 0; j < n; j++) {
        if (%(k)d && fread(v, 8, %(k)d, stdin) != %(k)d)
            return 2;
        int fd[2];
        if (pipe(fd) != 0)
            return 2;
        fflush(stdout);
        pid_t p = fork();
        if (p < 0)
            return 2;
        if (p == 0) {
            close(fd[0]);
            uint64_t u = (uint64_t)calc(%(args)s);
            if (write(fd[1], &u, 8) != 8)
                _exit(3);
            _exit(0);
        }
        close(fd[1]);
        uint64_t u = 0;
        ssize_t got = read(fd[0], &u, 8);
        close(fd[0]);
        int st = 0;
        if (waitpid(p, &st, 0) != p)
            return 2;
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0 && got == 8)
            printf("0 %%llu\n", (unsigned long long)u);
        else
            printf("1 0\n");
    }
    return 0;
}
'''

RUST_DRIVER = r'''
use std::io::Read;
fn main() {
    std::panic::set_hook(Box::new(|_| {}));
    let mut b = Vec::new();
    std::io::stdin().read_to_end(&mut b).unwrap();
    let n = u64::from_le_bytes(b[0..8].try_into().unwrap()) as usize;
    let k: usize = %(k)d;
    for j in 0..n {
        let base = 8 + 8 * k * j;
        let v: Vec<u64> = (0..k).map(|i| u64::from_le_bytes(b[base + 8 * i..base + 8 * i + 8].try_into().unwrap())).collect();
        let _ = &v;
        match std::panic::catch_unwind(|| calc(%(args)s)) {
            Ok(x) => println!("0 {}", %(bits)s),
            Err(_) => println!("1 0"),
        }
    }
}
'''


def sa(ctx, build, mode, data):
    return V.run([build / 'sa'], bytes([mode]) + data, timeout=60)


# ------------------------------------------------------------------ direction checks
class Run:
    def __init__(self, label):
        self.label, self.ok, self.fail, self.counts = label, 0, [], {}

    def check(self, cls, cond, what):
        self.counts[cls] = self.counts.get(cls, 0) + 1
        if cond:
            self.ok += 1
        else:
            self.fail.append('%s %s: %s' % (self.label, cls, what))
        return cond


def c_import_check(ctx, tool, quick=False, out=None):
    """Every positive C fixture: import, v3 codec decode/replay, reference == gcc UBSan oracle. Every refusal
    row: the exact exit. Returns (Run, {id: (env, ops, vs, oracle rows)})."""
    R_ = Run('c-import')
    units = {}
    cov = {}
    for cid, n, body, extra in C_POS:
        src = csrc(n, body)
        rc, env, err = V.run(tool, src.encode())
        if not R_.check('positive', rc == 0 and env, '%s exit %s %s' % (cid, rc, err[:120])):
            if quick:
                return R_, units
            continue
        rc, dec, _ = sa(ctx, ctx.a.s2build, 2, env)
        R_.check('positive', rc == 0 and dec == env, '%s v3 decode exit %s' % (cid, rc))
        rc, rep, _ = sa(ctx, ctx.a.s2build, 5, env)
        R_.check('positive', rc == 0 and rep == src.encode(), '%s provenance replay exit %s' % (cid, rc))
        hdr, ops = records(env)
        R_.check('positive', (hdr['ver'], hdr['lang'], hdr['prof'], hdr['rt'], hdr['abi']) == (3, 1, 10, 0, 1),
                 '%s header %s' % (cid, hdr))
        vs = vectors(n, cid, extra)
        orc, why = ctx.c_oracle(src, n, vs)
        if not R_.check('oracle', orc is not None, '%s gcc oracle: %s' % (cid, why)):
            continue
        c = set()
        bad = None
        for v, o in zip(vs, orc):
            ref = evaluate(ops, v, c)
            if not same(ref, o):
                bad = (v, ref, o)
                break
        cls = 'overflow' if any(o[0] for o in orc) else 'boundary'
        R_.check(cls, bad is None, '%s vector %s reference %s oracle %s' % ((cid,) + bad if bad else (cid, 0, 0, 0)))
        cov[cid] = (ops, c)
        units[cid] = (env, ops, vs, orc, src)
        if quick and R_.fail:
            return R_, units
    for cid, text, want in C_REFUSE:
        rc, o, e = V.run(tool, text.encode())
        cls = 'malformed' if want in (65, 72) else 'unsupported'
        R_.check(cls, rc == want and o == b'', '%s exit %s want %s' % (cid, rc, want))
        if quick and R_.fail:
            return R_, units
    # Function name (amendment A8): any C identifier of at most 63 bytes outside the reserved sets; the
    # semantic identity does not depend on it, and every accepted source compiles under gcc.
    body = 'a < b ? b - a : a - b'
    rc, e0, _ = V.run(tool, csrc(2, body).encode())
    rc0, id0, _ = sa(ctx, ctx.a.s2build, 3, e0) if rc == 0 else (rc, b'', b'')
    for name in ['Absdiff_2', '_x', 'x', 'omega_f', 'calc2', 'int_add', 'INT64', 'MAX_C2', 'size', 'n' * 63]:
        src = csrc(2, body).replace('calc(', name + '(', 1)
        rc, env, _ = V.run(tool, src.encode())
        rc2, ident, _ = sa(ctx, ctx.a.s2build, 3, env) if rc == 0 else (rc, b'', b'')
        d = ctx.tmp('c-name')
        (d / 'unit.c').write_text(src)
        rc3, _, err3 = V.run([ctx.a.cc, '-std=c17', '-Wall', '-Wextra', '-Werror', '-c', d / 'unit.c', '-o',
                              d / 'unit.o'])
        R_.check('name', rc == 0 and rc0 == 0 and rc2 == 0 and ident == id0 and rc3 == 0,
                 'name %r import %s identity %s gcc %s' % (name, rc, ident == id0, rc3))
        if quick and R_.fail:
            return R_, units
    for name in ['n' * 64, 'int8_t', 'size_t', 'INT64_MAX', 'UINT64_C', 'INT_WIDTH', 'INTMAX_MIN', '__x', '_Foo',
                 'main', 'bool', 'true', 'typeof', 'return', 'int64_t', '_Bool']:
        src = csrc(2, body).replace('calc(', name + '(', 1)
        rc, o, _ = V.run(tool, src.encode())
        R_.check('unsupported', rc == 71 and o == b'', 'name %r exit %s want 71' % (name, rc))
        if quick and R_.fail:
            return R_, units
    if out is not None:
        # Coverage: every comparison outcome and both arms of every select occur on some vector.
        miss = []
        for cid, (ops, c) in cov.items():
            for k, r in enumerate(ops):
                if 10 <= r['op'] <= 16 and not ({(k, 0), (k, 1)} <= c):
                    miss.append('%s#%d' % (cid, k))
        out['coverage_missing'] = miss
        R_.check('coverage', not miss, 'uncovered outcomes %s' % miss)
        out['overflow_error_vectors'] = sum(sum(o[0] for o in u[3]) for u in units.values())
        out['vectors'] = sum(len(u[2]) for u in units.values())
    return R_, units


def all_units(ctx, cunits):
    """(id, env, ops, vectors) for every C import result and every hand-built v3 unit (codec-accepted)."""
    out = []
    for cid, (env, ops, vs, orc, src) in cunits.items():
        out.append((cid, env, ops, vs))
    for hid, ops, lang, prof in HAND:
        env = env_of(sem(ops, lang, prof))
        rc, _, err = sa(ctx, ctx.a.s2build, 2, env)
        if rc != 0:
            raise AssertionError('hand unit %s refused by the v3 codec: %s %s' % (hid, rc, err))
        n = arity(ops)
        out.append((hid, env, ops, vectors(n, hid)))
    return out


def export_refusals():
    v1 = V.envelope(V.build_sem([(9, U, U, 0), (9, U, U, 1), (2, 0, 1, 0)]))
    big = [IN(0)]
    for i in range(1, 40):
        big.append(AR(2, i - 1, i - 1, 5))
    ok = env_of(sem([IN(0), IN(1), AR(2, 0, 1)]))
    return [
        ('v1-unit', v1, 68),
        ('garbage', b'not an envelope', 65),
        ('empty', b'', 65),
        ('truncated', ok[:-3], 65),
        ('bad-record', env_of(sem([IN(0), R(18, 5, 0, 0)])), 65),
        ('lang-mismatch', env_of(sem([IN(0), IN(1), AR(2, 0, 1)], lang=2, prof=10)), 68),
        ('too-long', env_of(sem(big)), 67),
        ('oversize', ok + b'\0' * 8200, 65),
    ]


def rust_call(tool, env, name=None, raw=False):
    """The public command takes [NAME]; a raw mutant binary takes the c-export style frame directly."""
    if raw:
        nm = (name or '').encode()
        return V.run(tool, bytes([len(nm)]) + nm + env)
    return V.run(tool + ([name] if name is not None else []), env)


def has_attr6(ops):
    return any(r['op'] in (2, 3, 4) and r['t'] == 5 and r['at'] == 6 for r in ops)


def matrix_rows(R_, uid, ops, vs, got, setting, expect_r_error=False):
    """Check one setting's rows against the reference. D/RC: attribute 6 overflow is the error. R: the
    two's-complement wrapped value (rows where the checked reference errors are counted as REFINEMENT).
    expect_r_error is the mutated-expectation control: it demands the error from R as well."""
    bad = None
    refine = 0
    for v, g in zip(vs, got):
        chk = evaluate(ops, v)
        ref = evaluate(ops, v, wrap6=(setting == 'R' and not expect_r_error))
        if not same(ref, g):
            bad = (v, ref, g)
            break
        if setting == 'R' and chk[0] == 1:
            refine += 1
    errs = sum(g[0] for g in got)
    R_.counts['rows:' + setting] = R_.counts.get('rows:' + setting, 0) + len(got)
    R_.counts['error:' + setting] = R_.counts.get('error:' + setting, 0) + errs
    if refine:
        R_.counts['refinement:' + setting] = R_.counts.get('refinement:' + setting, 0) + refine
    cls = '%s:%s' % (setting, 'overflow' if errs or refine else 'boundary')
    return R_.check(cls, bad is None, '%s %s vector %s reference %s rust %s' % (
        (setting, uid) + bad if bad else (setting, uid, 0, 0, 0)))


def rust_check(ctx, tool, units, quick=False, raw=False, settings=('D',)):
    R_ = Run('rust')
    results = {}
    for uid, env, ops, vs in units:
        rc, rs, err = rust_call(tool, env, raw=raw)
        if not R_.check('positive', rc == 0 and rs, '%s exit %s %s' % (uid, rc, err[:120])):
            if quick:
                return R_, results
            continue
        text = rs.decode()
        if uid in RUST_GOLDEN:
            R_.check('golden', text == RUST_GOLDEN[uid], '%s text %r' % (uid, text))
        per = {}
        for s in settings:
            got, why = ctx.rust_run(text, ptypes(ops), ops[-1]['t'], vs, s)
            if not R_.check('positive', got is not None, '%s %s rustc: %s' % (s, uid, why)):
                continue
            matrix_rows(R_, uid, ops, vs, got, s)
            per[s] = got
        if len(settings) > 1 and not has_attr6(ops) and len(per) == len(settings):
            # Attribute 5 (wrapping_*) and comparison-only units: the build setting must not matter.
            R_.check('attr5-identical', all(per[s] == per[settings[0]] for s in settings),
                     '%s differs across %s' % (uid, settings))
        if 'D' in per:
            results[uid] = per
        if quick and R_.fail:
            return R_, results
    for rid, env, want in export_refusals():
        rc, o, e = rust_call(tool, env, raw=raw)
        R_.check('malformed' if want == 65 else 'unsupported', rc == want and o == b'',
                 '%s exit %s want %s' % (rid, rc, want))
        if quick and R_.fail:
            return R_, results
    # NAME (amendment A8): any C identifier of at most 63 bytes that is not a Rust keyword.
    uid, env, ops, vs = units[0]
    base = RUST_GOLDEN[uid]
    for name in ['absdiff', 'Foo_1', '_x', 'main', 'union', 'n' * 63]:
        rc, o, _ = rust_call(tool, env, name, raw)
        want = base.replace('pub fn calc(', 'pub fn %s(' % name, 1)
        if R_.check('name', rc == 0 and o.decode() == want, 'NAME %r exit %s %r' % (name, rc, o[:60])):
            d = ctx.tmp('rust-name')
            (d / 'lib.rs').write_bytes(o)
            rc2, _, err2 = V.run([ctx.a.rustc, '--edition', '2021', '--crate-type', 'lib', '-A', 'warnings',
                                  d / 'lib.rs', '-o', d / 'lib.rlib'])
            R_.check('name', rc2 == 0, 'NAME %r rustc %s' % (name, err2[:120]))
        if quick and R_.fail:
            return R_, results
    for name in ['fn', '_', 'self', 'Self', 'async', 'gen', '1x', 'a-b', 'é', 'n' * 64, 'x' * 255]:
        rc, o, _ = rust_call(tool, env, name, raw)
        R_.check('unsupported', rc == 64 and o == b'', 'NAME %r exit %s' % (name, rc))
        if quick and R_.fail:
            return R_, results
    return R_, results


def lower_check(ctx, tool, units, quick=False):
    R_ = Run('native')
    for uid, env, ops, vs in units:
        rc, om, err = V.run(tool, env)
        if not R_.check('positive', rc == 0 and om.startswith(b'note omega.universal.lower.s2.v1 semantic '),
                        '%s lower exit %s %s' % (uid, rc, err[:120])):
            if quick:
                return R_
            continue
        rc2, ident, _ = sa(ctx, ctx.a.s2build, 3, env)
        R_.check('positive', rc2 == 0 and om.split(b'\n', 1)[0].endswith(ident.hex().encode()),
                 '%s semantic identity note' % uid)
        got, why = ctx.native(om, arity(ops), vs)
        if not R_.check('native', got is not None, '%s native: %s' % (uid, why)):
            if quick:
                return R_
            continue
        bad = None
        for v, g in zip(vs, got):
            ref = evaluate(ops, v)
            if g[0] not in (0, 1) or not same(ref, g):
                bad = (v, ref, g)
                break
        R_.check('native', bad is None, '%s vector %s reference %s native %s' % ((uid,) + bad if bad else (uid, 0, 0, 0)))
        if quick and R_.fail:
            return R_
    uid, env, ops, vs = units[0]
    rc, om, _ = V.run(tool + ['isum'], env)
    got, why = ctx.native(om, arity(ops), vs, 'omega_isum') if rc == 0 else (None, 'exit %s' % rc)
    R_.check('native', got is not None and all(same(evaluate(ops, v), g) for v, g in zip(vs, got)),
             'NAME isum: %s' % why)
    for name in ['1x', 'wadd', 'u', 'p', 'v', 'sbits', 'long', 'a-b', 'x' * 64]:
        rc, o, _ = V.run(tool + [name], env)
        R_.check('unsupported', rc == 64 and o == b'', 'NAME %r exit %s' % (name, rc))
    for rid, env2, want in export_refusals():
        if rid in ('too-long',):
            continue
        rc, o, _ = V.run(tool, env2)
        want2 = 68 if want == 68 else 65
        R_.check('malformed' if want2 == 65 else 'unsupported', rc == want2 and o == b'',
                 '%s exit %s want %s' % (rid, rc, want2))
    return R_


def compose_check(ctx, cunits, rust_results, settings=MATRIX):
    """C -> c-i64-import -> rust-i64-export -> rustc against the C oracle, per build setting. D and RC equal
    the oracle on every vector (UBSan error = panic). R equals it wherever C is defined; where the C oracle
    reports signed overflow (undefined behaviour in C17) R must return exactly the wrapped reference value:
    a REFINEMENT, counted separately, never an equality with the C error."""
    R_ = Run('compose')
    for cid, (env, ops, vs, orc, src) in cunits.items():
        per = rust_results.get(cid, {})
        for s in settings:
            got = per.get(s)
            if not R_.check('%s:composition' % s, got is not None, '%s %s no Rust result' % (s, cid)):
                continue
            bad = None
            for v, o, g in zip(vs, orc, got):
                if s == 'R' and o[0] == 1:
                    w = evaluate(ops, v, wrap6=True)
                    if g != w:
                        bad = (v, o, g)
                        break
                    R_.counts['R:refinement'] = R_.counts.get('R:refinement', 0) + 1
                elif not same(o, g):
                    bad = (v, o, g)
                    break
            R_.check('%s:composition' % s, bad is None,
                     '%s %s vector %s C %s Rust %s' % ((s, cid) + bad if bad else (s, cid, 0, 0, 0)))
    return R_


def control_check(ctx, tool, units):
    """Negative controls; each must FAIL its check. (i) mutated expectation: R is required to report the
    attribute 6 error. (ii) mutated unit: attribute 6 exported as wrapping_add, checked under D and RC."""
    out = []
    byid = {u[0]: u for u in units}
    uid, env, ops, vs = byid['add']
    rc, rs, _ = rust_call(tool, env)
    text = rs.decode() if rc == 0 else ''
    got, why = ctx.rust_run(text, ptypes(ops), ops[-1]['t'], vs, 'R')
    probe = Run('control')
    ok = got is not None and matrix_rows(probe, uid, ops, vs, got, 'R', expect_r_error=True)
    out.append({'control': 'R-reports-error-expectation', 'observed': 'PASS' if ok else 'FAIL',
                'by': probe.fail[:1] or [why]})
    mut = text.replace('i0 + i1', 'i0.wrapping_add(i1)')
    for s in ('D', 'RC'):
        probe = Run('control')
        got, why = ctx.rust_run(mut, ptypes(ops), ops[-1]['t'], vs, s)
        ok = mut != text and got is not None and matrix_rows(probe, uid, ops, vs, got, s)
        out.append({'control': 'attr6-as-wrapping_add-%s' % s, 'observed': 'PASS' if ok else 'FAIL',
                    'by': probe.fail[:1] or [why]})
    return out


# ------------------------------------------------------------------ codec (v1/v2 differential, v3 rows)
def codec_check(ctx):
    R_ = Run('codec')
    old, new = ctx.a.sa2 / 'sa', ctx.a.s2build / 'sa'
    base = []
    s1 = V.build_sem([(9, U, U, 0), (9, U, U, 1), (2, 0, 1, 0), (1, U, U, 7), (4, 2, 3, 0), (1, U, U, 0), (2, 4, 5, 0)])
    base.append(V.envelope(s1))
    rc, e, _ = V.run([ctx.a.c_import_v1], b'#include <stdint.h>\nuint64_t calc(uint64_t a, uint64_t b) { return a * b + 3ULL; }\n')
    if rc == 0:
        base.append(e)
    for lang, prof in [(5, 7), (6, 8), (7, 9)]:
        s = bytearray(V.build_sem([(9, U, U, 0), (1, U, U, 3), (3, 0, 1, 0)], lang=lang, prof=prof))
        s[4] = 2
        base.append(V.envelope(bytes(s)))
    for lang, prof in [(3, 3), (3, 4), (4, 5)]:
        base.append(V.envelope(V.build_sem([(9, U, U, 0), (2, 0, 0, 0)], lang=lang, prof=prof)))
    corpus = list(base)
    rnd = random.Random('S2-codec-corpus')
    for e in base:
        for pos in range(min(len(e), 13 + 44 + 40)):
            for x in (0x01, 0x02, 0x80):
                m = bytearray(e)
                m[pos] ^= x
                corpus.append(bytes(m))
        for _ in range(40):
            m = bytearray(e)
            m[rnd.randrange(len(m))] = rnd.randrange(256)
            corpus.append(bytes(m))
        corpus.append(e[:-1])
        corpus.append(e + b'\0')
    diffs = 0
    # A mutant whose OMSA version byte became 3 is a v3 input, outside the "v1/v2 unchanged" domain; the v3
    # rows below cover that domain (e.g. v3-profile-1 refuses 68 where the v2 codec refuses 66).
    v3dom = [e for e in corpus if len(e) > 17 and e[:4] == b'OMSB' and e[13:17] == b'OMSA' and e[17] == 3]
    R_.counts['v3-domain-excluded'] = len(v3dom)
    for e in corpus:
        if len(e) > 17 and e[:4] == b'OMSB' and e[13:17] == b'OMSA' and e[17] == 3:
            continue
        for mode in range(0, 8):
            data = (bytes([1, 0, 1, 0]) + e) if mode == 6 else e
            a = sa(ctx, ctx.a.sa2, mode, data)
            b = sa(ctx, ctx.a.s2build, mode, data)
            if (a[0], a[1]) != (b[0], b[1]):
                diffs += 1
                if diffs <= 5:
                    R_.fail.append('codec v1/v2 differential mode %d: v2 %s v3 %s on %s' % (mode, a[0], b[0], e[:30].hex()))
            R_.counts['v1v2-differential'] = R_.counts.get('v1v2-differential', 0) + 1
    R_.ok += R_.counts['v1v2-differential'] - diffs
    good = [IN(0), IN(1), AR(2, 0, 1), CMP(12, 0, 1), SEL(3, 2, 0)]
    rows = [
        ('v3-ok', sem(good), 0),
        ('version-4', sem(good, ver=4), 66),
        ('v3-profile-1', sem(good, prof=1), 68),
        ('v2-profile-10', sem(good, ver=2), 68),
        ('profile-16', sem(good, prof=16), 68),
        ('lang-2-profile-10', sem(good, lang=2), 68),
        ('runtime-1', sem(good, rt=1), 68),
        ('abi-2', sem(good, abi=2), 68),
        ('op-18', sem([IN(0), R(18, 5, 0, 0)]), 69),
        ('op-0', sem([IN(0), R(0, 5, 0, 0)]), 69),
        ('op-5-load', sem([IN(0), R(5, 5, 0, 0)]), 68),
        ('type-7', sem([IN(0), R(2, 7, 6, 33, 0, 0)]), 70),
        ('type-2', sem([IN(0), R(2, 2, 6, 33, 0, 0)]), 68),
        ('effects', sem([IN(0), R(2, 5, 6, 33, 0, 0, ef=1)]), 71),
        ('attr6-obl1', sem([IN(0), R(2, 5, 6, 1, 0, 0)]), 72),
        ('attr5-obl33', sem([IN(0), R(2, 5, 5, 33, 0, 0)]), 72),
        ('i64-attr1', sem([IN(0), R(2, 5, 1, 1, 0, 0)]), 70),
        ('arith-imm', sem([IN(0), R(2, 5, 6, 33, 0, 0, imm=1)]), 65),
        ('arith-forward', sem([IN(0), R(2, 5, 6, 33, 0, 2), IN(1)]), 70),
        ('arith-type-mix', sem([IN(0), IN(1, 1), R(2, 5, 6, 33, 0, 1)]), 70),
        ('bool-arith', sem([IN(0), CMP(10, 0, 0), R(2, 6, 0, 0, 1, 1)]), 70),
        ('cmp-type-i64', sem([IN(0), R(12, 5, 0, 0, 0, 0)]), 70),
        ('cmp-mixed', sem([IN(0), IN(1, 1), CMP(12, 0, 1)]), 70),
        ('cmp-bool-operands', sem([IN(0), CMP(10, 0, 0), CMP(10, 1, 1)]), 70),
        ('cmp-obl', sem([IN(0), R(12, 6, 0, 1, 0, 0)]), 72),
        ('sel-cond-i64', sem([IN(0), SEL(0, 0, 0)]), 70),
        ('sel-else-forward', sem([IN(0), CMP(10, 0, 0), SEL(1, 0, 3), IN(1)]), 70),
        ('sel-else-type', sem([IN(0), IN(1, 1), CMP(10, 0, 0), SEL(2, 0, 1)]), 70),
        ('sel-attr', sem([IN(0), CMP(10, 0, 0), R(16, 5, 5, 0, 1, 0, 0)]), 70),
        ('not-b', sem([IN(0), CMP(10, 0, 0), R(17, 6, 0, 0, 1, 0)]), 70),
        ('not-i64', sem([IN(0), NOT(0)]), 70),
        ('bool-const-2', sem([R(1, 6, 0, 0, imm=2)]), 70),
        ('i64-const-attr1', sem([R(1, 5, 1, 0, imm=2)]), 70),
        ('u64-const-attr0', sem([R(1, 1, 0, 0, imm=2)]), 70),
        ('bool-input', sem([IN(0, 6)]), 70),
        ('input-slot-6', sem([IN(6)]), 67),
        ('input-dup', sem([IN(0), IN(0)]), 70),
        ('input-obl', sem([R(9, 5, 0, 0, imm=0)]), 72),
        ('dialect', sem([R(9, 5, 0, 1, imm=0, d=2)]), 68),
        ('op-version', sem([R(9, 5, 0, 1, imm=0, v=2)]), 66),
    ]
    for rid, s, want in rows:
        rc, o, _ = sa(ctx, ctx.a.s2build, 2, env_of(s))
        R_.check('v3-positive' if want == 0 else 'v3-refusal', rc == want, '%s exit %s want %s' % (rid, rc, want))
    rc, _, _ = sa(ctx, ctx.a.sa2, 2, env_of(sem(good)))
    R_.check('v3-refusal', rc == 66, 'v2 codec on a v3 unit exit %s want 66' % rc)
    return R_


# ------------------------------------------------------------------ tool-source mutants
C_MUTANTS = [
    ('wrap-as-plain', 'setf 5 5 1\nret emitop code a b 0 lo hi', 'setf 5 6 33\nret emitop code a b 0 lo hi'),
    ('plain-add-as-wrap', 'right = term depth\nsetf 5 6 33', 'right = term depth\nsetf 5 5 1'),
    ('plain-mul-as-wrap', 'right = primary depth\nsetf 5 6 33', 'right = primary depth\nsetf 5 5 1'),
    ('lt-as-le', 'lexnext\nret 13\nend\nret 12', 'lexnext\nret 13\nend\nret 13'),
    ('gt-as-ge', 'lexnext\nret 15\nend\nret 14', 'lexnext\nret 15\nend\nret 15'),
    ('eq-as-ne', 'lexnext\nret 10', 'lexnext\nret 11'),
    ('select-arms-swapped', 'ret emitop 16 c x y qlo qhi', 'ret emitop 16 c y x qlo qhi'),
    ('compare-operands-swapped', 'c = emitop code left right 0 lo hi', 'c = emitop code right left 0 lo hi'),
    ('wrap-sub-as-add', 'if eq k 45\ncode = 3\nend\nif eq k 42', 'if eq k 45\ncode = 2\nend\nif eq k 42'),
    ('literal-limit', 'gt d 7', 'gt d 8'),
    ('sub-as-add', 'if eq get Tk 0 45\ncode = 3', 'if eq get Tk 0 45\ncode = 2'),
]
RUST_MUTANTS = [
    ('attr6-as-wrapping', 'ret and eq get Ot k 5 eq get Oat k 6', 'ret and eq get Ot k 5 eq get Oat k 5'),
    ('lt-as-le', 'text Ycmp " ==  !=  <   <=  >   >= "', 'text Ycmp " ==  !=  <=  <=  >   >= "'),
    ('eq-as-ne', 'text Ycmp " ==  !=  <   <=  >   >= "', 'text Ycmp " !=  !=  <   <=  >   >= "'),
    ('select-arms-swapped', 'yemit get Ob k 1\nfor i ulong len Ysel3\nybyte ulong get Ysel3 i\nend\nyemit get Oi k 1',
     'yemit get Oi k 1\nfor i ulong len Ysel3\nybyte ulong get Ysel3 i\nend\nyemit get Ob k 1'),
    ('negative-sign-dropped', 'ybyte 45\nydec sub 0 v', 'ybyte 32\nydec sub 0 v'),
    ('i64-suffix-as-u64', 'if eq t 5\nybyte 105\nelse', 'if eq t 5\nybyte 117\nelse'),
    ('wrapping-add-sub-swapped', 'text Ymeth ".wrapping_add(.wrapping_sub(', 'text Ymeth ".wrapping_sub(.wrapping_add('),
    ('plain-add-sub-swapped', 'text Yops " + - * "', 'text Yops " - + * "'),
    ('not-dropped', 'ybyte 33\nyemit get Oa k 0', 'ybyte 32\nyemit get Oa k 0'),
    ('normalization-kept', 'if ne get Oat y 5\nret y', 'if ne get Oat y 6\nret y'),
]
LOWER_MUTANTS = [
    ('add-overflow-edge', 'if and lt b 0 ge r 0\\nret 1', 'if and lt b 0 gt r 0\\nret 1'),
    ('mul-min-neg1', 'if eq b -9223372036854775808\\nret 1', 'if eq b -9223372036854775808\\nret 0'),
    ('sub-overflow-sign', 'if and ge b 0 ge r 0\\nret 1', 'if and ge b 0 lt r 0\\nret 1'),
    ('select-cond-poison-dropped', 'ofrag 35\nogp c\nogp x', 'ofrag 35\nogp x\nogp x'),
    ('select-arms-swapped', 'oarm k c get Ob k\nofrag 17\noarm k c get Oi k', 'oarm k c get Oi k\nofrag 17\noarm k c get Ob k'),
    ('compare-shifted', 'ofrag add get Oc k 19', 'ofrag add get Oc k 20'),
    ('attr6-overflow-ignored', 'if eq get Oat k 6\nopp k\nofrag add c 24', 'if eq get Oat k 7\nopp k\nofrag add c 24'),
    ('root-trap-inverted', 'ofrag 16\nofrag 30\nogp r', 'ofrag 16\nofrag 29\nogp r'),
    ('wsub-operands-swapped', 'ret slong sub sbits a sbits b', 'ret slong sub sbits b sbits a'),
    ('not-as-add', 'ofrag 40', 'ofrag 20'),
    ('compare-poison-dropped', 'ofrag 35\nogp a\nogp get Ob k', 'ofrag 35\nogp get Ob k\nogp get Ob k'),
]


def build_mutant(ctx, name, src_rel, old, new):
    m = json.loads((ctx.a.s2build / 'build.json').read_text())['artifacts'][name]
    pin = m['compiler_sha256']
    comp = ctx.a.omc191 if pin == SEED_OMC else ctx.a.omc
    text = (ROOT / src_rel).read_text()
    if text.count(old) != 1:
        raise AssertionError('mutant anchor %r occurs %d times in %s' % (old, text.count(old), src_rel))
    parts = []
    for rel in m['inputs']:
        parts.append(text.replace(old, new).encode() if rel == src_rel else (ROOT / rel).read_bytes())
    program = b'\n'.join(parts) + b'\n'
    rc, binary, err = V.run([comp], program, timeout=120)
    if rc != 0 or binary[:4] != b'\x7fELF':
        return None, 'compile exit %s: %s' % (rc, err.decode(errors='replace')[:200])
    d = ctx.tmp('mutant-' + name)
    exe = d / name
    exe.write_bytes(binary)
    exe.chmod(0o700)
    return [exe], None


def run_mutants(ctx, cunits, units):
    res = []
    plan = [('c-i64-import', 'src/adapters/c-i64-import.om', C_MUTANTS),
            ('rust-i64-export', 'src/adapters/rust-i64-export.om', RUST_MUTANTS),
            ('lower-s2', 'src/adapters/upper-lower-s2.om', LOWER_MUTANTS)]
    for name, rel, muts in plan:
        for mid, old, new in muts:
            tool, why = build_mutant(ctx, name, rel, old, new)
            if tool is None:
                res.append({'tool': name, 'mutant': mid, 'killed': False, 'why': 'stillborn: ' + why})
                continue
            if name == 'c-i64-import':
                r, _ = c_import_check(ctx, tool, quick=True)
            elif name == 'rust-i64-export':
                r, _ = rust_check(ctx, tool, units, quick=True, raw=True)
            else:
                r = lower_check(ctx, tool, units, quick=True)
            res.append({'tool': name, 'mutant': mid, 'killed': bool(r.fail), 'by': r.fail[:1]})
    return res


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--omc', required=True)
    ap.add_argument('--omc191', required=True)
    ap.add_argument('--bridge-native', required=True)
    ap.add_argument('--sa2', required=True, help='the v2 codec build directory (build/L832)')
    ap.add_argument('--s2build', default=str(ROOT / 'build/S2'))
    ap.add_argument('--c-import-v1', required=True)
    ap.add_argument('--cc', default='gcc')
    ap.add_argument('--rustc', default='rustc')
    ap.add_argument('--out', required=True)
    ap.add_argument('--log-dir', default='')
    a = ap.parse_args()
    for k in ('sa2', 's2build'):
        setattr(a, k, Path(getattr(a, k)))
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=False)
    rep = {'schema': 'omega.translation-s2.selftest.v1', 'spec': 'spec/universal/translation-s2-v1.md', 'tools': {}}
    try:
        for k, pin in (('omc', NATIVE_OMC), ('omc191', SEED_OMC)):
            p = Path(getattr(a, k))
            if not p.is_file() or sha(p.read_bytes()) != pin:
                raise NotRun('%s is not the pinned compiler %s' % (k, pin[:8]))
            rep['tools'][k] = {'path': str(p), 'sha256': pin}
        for k in ('bridge_native', 'c_import_v1'):
            p = Path(getattr(a, k))
            if not p.is_file():
                raise NotRun('missing ' + k)
            rep['tools'][k] = {'path': str(p), 'sha256': sha(p.read_bytes())}
        for k, p in (('sa2', a.sa2 / 'sa'), ('s2-sa', a.s2build / 'sa')):
            if not p.is_file():
                raise NotRun('missing ' + str(p))
            rep['tools'][k] = {'path': str(p), 'sha256': sha(p.read_bytes())}
        for k, argv in (('cc', [a.cc, '--version']), ('rustc', [a.rustc, '--version'])):
            rc, o, _ = V.run(argv)
            if rc != 0:
                raise NotRun('oracle %s absent' % k)
            rep['tools'][k] = o.decode().split('\n')[0]
        rep['tools']['s2_build'] = json.loads((a.s2build / 'build.json').read_text())
    except NotRun as e:
        print('NOT_RUN', e)
        (out / 'receipt.json').write_text(json.dumps({'status': 'NOT_RUN', 'why': str(e)}, indent=2) + '\n')
        return 3
    t0 = time.time()
    ctx = Ctx(a, out / 'work')
    tools = {k: [str(ROOT / 'tools/adapters' / k)] for k in ('c-i64-import', 'rust-i64-export', 'lower-s2')}
    runs = []
    runs.append(codec_check(ctx))
    extra = {}
    rc_run, cunits = c_import_check(ctx, tools['c-i64-import'], out=extra)
    runs.append(rc_run)
    units = all_units(ctx, cunits)
    rr, rust_results = rust_check(ctx, tools['rust-i64-export'], units, settings=MATRIX)
    runs.append(rr)
    runs.append(lower_check(ctx, tools['lower-s2'], units))
    runs.append(compose_check(ctx, cunits, rust_results))
    controls = control_check(ctx, tools['rust-i64-export'], units)
    muts = run_mutants(ctx, cunits, units)
    fails = [f for r in runs for f in r.fail]
    per = {}
    for m in muts:
        per.setdefault(m['tool'], [0, 0])
        per[m['tool']][0] += 1
        per[m['tool']][1] += int(m['killed'])
    for tool, (n, k) in per.items():
        if n < 8 or k != n:
            fails.append('mutants %s: %d/%d killed (need all, >= 8)' % (tool, k, n))
    for c in controls:
        if c['observed'] != 'FAIL':
            fails.append('negative control %s passed (must fail)' % c['control'])
    matrix = {}
    for r in (rr, runs[-1]):
        for k, v in r.counts.items():
            if ':' in k:
                s = k.split(':')[1] if k.split(':')[0] in ('rows', 'error', 'refinement') else k.split(':')[0]
                matrix.setdefault(s, {})['%s.%s' % (r.label, k)] = v
    rep.update({
        'status': 'PASS' if not fails else 'FAIL',
        'checks': {r.label: {'ok': r.ok, 'failed': len(r.fail), 'by_class': r.counts} for r in runs},
        'c_import': extra, 'units': len(units), 'c_fixtures': len(cunits), 'hand_units': len(HAND),
        'mutants': muts, 'mutant_summary': {k: {'total': n, 'killed': kk} for k, (n, kk) in per.items()},
        'rust_settings': {s: RUST_SETTINGS[s] for s in MATRIX}, 'rust_matrix': matrix, 'controls': controls,
        'failures': fails[:50], 'seconds': round(time.time() - t0, 1),
    })
    text = json.dumps(rep, indent=2) + '\n'
    (out / 'receipt.json').write_text(text)
    if a.log_dir:
        Path(a.log_dir).mkdir(parents=True, exist_ok=True)
        (Path(a.log_dir) / ('s2-selftest-%s.json' % out.name)).write_text(text)
    for r in runs:
        print('%-9s ok %5d failed %3d %s' % (r.label, r.ok, len(r.fail), json.dumps(r.counts, sort_keys=True)))
    for tool, (n, k) in sorted(per.items()):
        print('mutants %-16s %d/%d killed' % (tool, k, n))
    for s in MATRIX:
        print('rust-setting %-2s %s' % (s, json.dumps(matrix.get(s, {}), sort_keys=True)))
    for c in controls:
        print('control %-32s observed %s %s' % (c['control'], c['observed'], (c['by'] or [''])[0]))
    for f in fails[:30]:
        print('FAIL', f)
    print('TRANSLATION-S2 %s receipt %s' % (rep['status'], out / 'receipt.json'))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
