#!/usr/bin/env python3
"""Independent source-aware v2 reference decoder (#623), written from spec/universal/source-aware-v2.md
(section 5 decode order, normative for v1 and v2) and the v1 typed registry it keeps; no codec source is
read. `--sa` runs a differential against a native codec (build/L832/sa): every request goes to the native
binary as one process (mode byte + envelope on stdin) and to this decoder; exit status and stdout must
agree exactly. Modes compared: 1 encode, 2 decode, 3 identity, 4 provenance, 5 replay, plus the mode and
size checks of `perform`. Mode 6 legality is not modelled (its per-operation reply is not specified
byte for byte in either spec) and is reported NOT_RUN.

Corpus: every F1 import (C importer), every F2 import of each available importer, R(U, M) for the six
family members of each, the v2 corner cases (version/profile pairing, wrong dialect, obligation 4 on
profiles 7..9, division, ops 6/7/8, every version byte value, every truncation, header fields) and seeded
random byte mutations of valid envelopes (raw, with the sidecar hashes resealed, and semantic-only).
Exit 0 when every comparison agrees, 1 on any disagreement, 3 when the codec or a pin is absent.
"""
from pathlib import Path
import argparse, hashlib, json, os, random, struct, subprocess, sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[3]
SPEC = ('spec/universal/source-aware-v2.md', '2f894c7b923db7e4e89d7ed7339dae97cdb2969742e7b1ed2026637007edb5c7')
SEMANTICS = 'spec/universal/semantics.md'
U = 65535
MAX_INPUT, MAX_SEM, MAX_SIDE, MAX_SRC = 8192, 1324, 4682, 4096
# profile -> (language, allowed (runtime, ABI) pairs, allowed types)
PROFILES = {1: (1, {(0, 1)}, {1}), 2: (2, {(0, 1)}, {1}), 3: (3, {(1, 1), (2, 1)}, {2}),
            4: (3, {(0, 1)}, {1}), 5: (4, {(0, 1)}, {1}), 6: (3, {(1, 2), (2, 2)}, {1, 2, 3, 4}),
            7: (5, {(0, 1)}, {1}), 8: (6, {(0, 1)}, {1}), 9: (7, {(0, 1)}, {1})}
# family members (translation-compose-v2 section 1): OMSA version, language, profile, dialect, arith obligations
MEMBERS = {'c': (1, 1, 1, 1, 1), 'rust': (1, 2, 2, 2, 1), 'python': (1, 3, 4, 3, 5),
           'cpp': (2, 5, 7, 5, 1), 'zig': (2, 6, 8, 6, 1), 'go': (2, 7, 9, 7, 1)}


def semantics_digest(root=ROOT):
    return hashlib.sha256((root / SEMANTICS).read_bytes()).digest()


class Ref:
    def __init__(self, digest):
        self.digest = digest

    def decode(self, b):
        """Returns (status, fields). Section 5 order; the first failure wins."""
        if len(b) > MAX_INPUT:
            return 67, None
        if len(b) < 13 or b[:4] != b'OMSB':
            return 65, None
        if b[4] != 1:
            return 66, None
        ns, np_ = struct.unpack_from('<II', b, 5)
        if ns > MAX_SEM or np_ > MAX_SIDE:
            return 67, None
        if len(b) != 13 + ns + np_ or ns < 44:
            return 65, None
        s = b[13:13 + ns]
        if s[:4] != b'OMSA':
            return 65, None
        if s[4] not in (1, 2) or s[7] != 1:
            return 66, None
        count = struct.unpack_from('<H', s, 10)[0]
        if count == 0 or count > 64:
            return 67, None
        if ns != 44 + 20 * count:
            return 65, None
        if s[12:44] != self.digest:
            return 78, None
        version, lang, prof, rt, abi = s[4], s[5], s[6], s[8], s[9]
        if prof == 0 or prof > 9 or lang != PROFILES[prof][0] or (version == 2) != (prof >= 7) \
                or (rt, abi) not in PROFILES[prof][1]:
            return 68, None
        types, slots, ops, aliases = [], set(), [], []
        for i in range(count):
            d, v, op, t, attr, eff, alias, obl, A, B, imm = struct.unpack_from('<8BHHQ', s, 44 + 20 * i)
            if d != lang:
                return 68, None
            if v != 1:
                return 66, None
            if t == 0 or t > 4:
                return 70, None
            if t not in PROFILES[prof][2]:
                return 68, None
            if op == 0 or op > 9:
                return 69, None
            st = self.row(prof, i, op, t, attr, eff, alias, obl, A, B, imm, types, slots, aliases)
            if st:
                return st, None
            types.append(t)
            aliases.append(alias)
            ops.append((op, A, B, imm))
        side = b[13 + ns:]
        if np_:
            if np_ < 74 or side[:4] != b'OMSP':
                return 65, None
            if side[4] != 1:
                return 66, None
            if side[5] > 1:
                return 76, None
            n = struct.unpack_from('<I', side, 6)[0]
            if n > MAX_SRC:
                return 67, None
            if np_ != 74 + 8 * count + n:
                return 65, None
            src = side[74 + 8 * count:]
            if side[10:42] != hashlib.sha256(s).digest() or side[42:74] != hashlib.sha256(src).digest():
                return 74, None
            if side[5] == 1:
                try:
                    src.decode('utf-8', errors='strict')  # strict: no overlong, surrogate or > U+10FFFF
                except UnicodeDecodeError:
                    return 76, None
            for k in range(count):
                lo, hi = struct.unpack_from('<II', side, 74 + 8 * k)
                if lo > hi or hi > n:
                    return 75, None
                if side[5] == 1 and any(x != n and 0x80 <= src[x] <= 0xBF for x in (lo, hi)):
                    return 75, None
        else:
            src = None
        return 0, {'sem': s, 'source': src, 'ops': ops, 'profile': prof, 'version': version}

    @staticmethod
    def row(prof, i, op, t, attr, eff, alias, obl, A, B, imm, types, slots, aliases):
        """One operation row, transcribed check by check from source-aware-v2.md section 5 step 6
        (sha256 2f894c7b...); within a row the first failing check decides."""
        def earlier(x, want):  # an operand index < i whose record has type `want` (65535 is never one)
            return x < i and types[x] == want

        if op == 1:
            if t > 2 or attr != t or A != U or B != U:
                return 70
            if obl != 0:
                return 72
            if eff != 0 or alias != 0:
                return 71
            return 0
        if op in (2, 3, 4, 5):
            if t > 2 or attr != t or not earlier(A, t) or not earlier(B, t):
                return 70
            if imm != 0:
                return 65
            if t == 1 and (eff != 0 or alias != 0):
                return 71
            if t == 2 and (eff != 20 or alias != 255):
                return 71
            want = (1 | (4 if prof in (4, 6) else 0)) if t == 1 else 8
            if op == 5:
                want += 2
            return 72 if obl != want else 0
        if op == 6:
            if prof != 6:
                return 68
            if t != 3 or attr != 3 or not earlier(A, 3) or B != U:
                return 70
            if imm != 0:
                return 65
            if eff != 63 or alias != 255:
                return 71
            return 72 if obl != 8 else 0
        if op in (7, 8):
            if prof != 6:
                return 68
            if t != 1 or attr != 4 or not earlier(A, 4) or not earlier(B, 1):
                return 70
            if not (1 <= alias <= 8 or alias == 255) or alias != aliases[A]:
                return 71
            if op == 8 and not earlier(imm, 1):
                return 70
            if op == 7 and imm != 0:
                return 65
            if eff != (17 if op == 7 else 19):
                return 71
            return 72 if obl != 24 else 0
        # op 9 (input)
        if attr != 0 or A != U or B != U:
            return 70
        if imm > 5:
            return 67
        if imm in slots:
            return 70
        slots.add(imm)
        if obl != 1:
            return 72
        if eff != 0:
            return 71
        if (t == 4 and not (1 <= alias <= 8 or alias == 255)) or (t != 4 and alias != 0):
            return 71
        return 0

    def perform(self, mode, b):
        """(status, stdout) of `sa` for mode byte + envelope."""
        if len(b) > MAX_INPUT:
            return 67, b''
        if mode == 0 or mode > 6:
            return 64, b''
        st, f = self.decode(b)
        if st:
            return st, b''
        if mode in (1, 2):
            return 0, b
        if mode == 3:
            return 0, hashlib.sha256(b'omega.source.semantic.v1' + f['sem']).digest()
        if mode == 4:
            return 0, hashlib.sha256(b'omega.source.provenance.v1' + b).digest()
        if mode == 5:
            return (73, b'') if f['source'] is None else (0, f['source'])
        raise ValueError('legality not modelled')


# ---------------------------------------------------------------- corpus construction

def envelope(sem, side=b''):
    return b'OMSB\x01' + struct.pack('<II', len(sem), len(side)) + sem + side


def split(env):
    ns, np_ = struct.unpack_from('<II', env, 5)
    return env[13:13 + ns], env[13 + ns:]


def R(sem, member):
    """translation-compose-v2 section 2: version, language, profile, dialects, arithmetic obligations."""
    ver, lang, prof, dialect, arith = MEMBERS[member]
    b = bytearray(sem)
    b[4], b[5], b[6] = ver, lang, prof
    for k in range(struct.unpack_from('<H', sem, 10)[0]):
        o = 44 + 20 * k
        b[o] = dialect
        if b[o + 2] in (2, 3, 4):
            b[o + 7] = arith
    return bytes(b)


def reseal(env):
    """Recompute the sidecar's semantic and source hashes after a mutation (when the framing allows)."""
    try:
        sem, side = split(env)
        if len(side) >= 74 and len(sem) >= 12:
            count = struct.unpack_from('<H', sem, 10)[0]
            src = side[74 + 8 * count:]
            side = side[:10] + hashlib.sha256(sem).digest() + hashlib.sha256(src).digest() + side[74:]
            return env[:13 + len(sem)] + side
    except struct.error:
        pass
    return env


def set_byte(b, off, v):
    return b[:off] + bytes([v]) + b[off + 1:]


def corner_cases(env, full_sweep):
    """Named corner cases derived from one valid envelope (with sidecar) of any family member."""
    sem, side = split(env)
    count = struct.unpack_from('<H', sem, 10)[0]
    out = []
    so = envelope(sem)
    base = {m: R(sem, m) for m in MEMBERS}
    for m, s in base.items():
        out.append(('R-%s' % m, envelope(s)))
    c = base['c']
    # version / profile pairing (section 3) and every profile/language under both versions
    for ver in (1, 2):
        for prof in range(0, 11):
            for lang in sorted({PROFILES.get(prof, (prof,))[0], 1, 5}):
                s = bytearray(c)
                s[4], s[5], s[6] = ver, lang, prof
                for k in range(count):
                    s[44 + 20 * k] = lang
                out.append(('ver%d-prof%d-lang%d' % (ver, prof, lang), envelope(bytes(s))))
    for m in ('cpp', 'zig', 'go'):
        s = base[m]
        k_arith = [k for k in range(count) if s[44 + 20 * k + 2] in (2, 3, 4)]
        for k in range(count):
            o = 44 + 20 * k
            out.append(('%s-wrong-dialect-op%d' % (m, k), envelope(set_byte(s, o, 1))))
            out.append(('%s-op-version-2-op%d' % (m, k), envelope(set_byte(s, o + 1, 2))))
            for opc in (5, 6, 7, 8, 0, 10):
                out.append(('%s-opcode%d-op%d' % (m, opc, k), envelope(set_byte(s, o + 2, opc))))
            for t in (0, 2, 3, 4, 5):
                out.append(('%s-type%d-op%d' % (m, t, k), envelope(set_byte(s, o + 3, t))))
        for k in k_arith:
            o = 44 + 20 * k
            out.append(('%s-obligation4-op%d' % (m, k), envelope(set_byte(s, o + 7, 5))))
            out.append(('%s-obligation0-op%d' % (m, k), envelope(set_byte(s, o + 7, 0))))
            div = set_byte(s, o + 2, 5)
            out.append(('%s-division-obl1-op%d' % (m, k), envelope(div)))
            out.append(('%s-division-obl3-op%d' % (m, k), envelope(set_byte(div, o + 7, 3))))
            out.append(('%s-division-obl7-op%d' % (m, k), envelope(set_byte(div, o + 7, 7))))
        for rt in range(0, 3):
            for abi in range(0, 4):
                out.append(('%s-rt%d-abi%d' % (m, rt, abi), envelope(set_byte(set_byte(s, 8, rt), 9, abi))))
    # every version byte, every value
    full = [('envelope', 4), ('omsa', 17), ('dialect-version', 20)]
    full += [('op%d-version' % k, 13 + 44 + 20 * k + 1) for k in range(count)]
    if side:
        full.append(('omsp', 13 + len(sem) + 4))
    for vname, off in (full if full_sweep else full[:3]):
        for v in (range(256) if full_sweep else (0, 1, 2, 3, 255)):
            if v != env[off]:
                out.append(('%s=%d' % (vname, v), set_byte(env, off, v)))
                e2 = envelope(R(sem, 'go'))
                if off < len(e2) and vname != 'omsp':
                    out.append(('go-%s=%d' % (vname, v), set_byte(e2, off, v)))
    # every truncation (with and without length fix-up) and simple extensions
    for n in range(len(env)):
        out.append(('truncate-%d' % n, env[:n]))
    out.append(('extend-1', env + b'\0'))
    out.append(('semantic-only', so))
    out.append(('oversize', env + b'\0' * (MAX_INPUT + 1 - len(env))))
    return out


def rec(lang, op, t, attr, eff, alias, obl, A=U, B=U, imm=0):
    return struct.pack('<8BHHQ', lang, 1, op, t, attr, eff, alias, obl, A, B, imm)


def sem_of(ver, lang, prof, rt, abi, recs, digest):
    return b'OMSA' + bytes([ver, lang, prof, 1, rt, abi]) + struct.pack('<H', len(recs)) + digest + b''.join(recs)


def constructed_units(digest):
    """Valid units of profiles 3 and 6 and of division in profiles 1/7, as named semantic-only envelopes."""
    p3 = [rec(3, 9, 2, 0, 0, 0, 1, imm=0), rec(3, 9, 2, 0, 0, 0, 1, imm=1), rec(3, 2, 2, 2, 20, 255, 8, 0, 1),
          rec(3, 1, 2, 2, 0, 0, 0, imm=7), rec(3, 5, 2, 2, 20, 255, 10, 2, 3)]
    p6u = [rec(3, 9, 1, 0, 0, 0, 1, imm=1), rec(3, 9, 1, 0, 0, 0, 1, imm=0), rec(3, 4, 1, 1, 0, 0, 5, 0, 1),
           rec(3, 1, 1, 1, 0, 0, 0, imm=3), rec(3, 3, 1, 1, 0, 0, 5, 2, 3)]
    p6v = [rec(3, 9, 4, 0, 0, 1, 1, imm=0), rec(3, 9, 1, 0, 0, 0, 1, imm=1), rec(3, 9, 3, 0, 0, 0, 1, imm=2),
           rec(3, 7, 1, 4, 17, 1, 24, 0, 1), rec(3, 8, 1, 4, 19, 1, 24, 0, 1, imm=3),
           rec(3, 6, 3, 3, 63, 255, 8, 2), rec(3, 9, 2, 0, 0, 0, 1, imm=3), rec(3, 1, 2, 2, 0, 0, 0, imm=9),
           rec(3, 2, 2, 2, 20, 255, 8, 6, 7)]
    d1 = [rec(1, 9, 1, 0, 0, 0, 1, imm=0), rec(1, 9, 1, 0, 0, 0, 1, imm=1), rec(1, 5, 1, 1, 0, 0, 3, 0, 1)]
    d7 = [rec(5, 9, 1, 0, 0, 0, 1, imm=0), rec(5, 9, 1, 0, 0, 0, 1, imm=1), rec(5, 5, 1, 1, 0, 0, 3, 0, 1)]
    return [('profile3-bigint', envelope(sem_of(1, 3, 3, 1, 1, p3, digest))),
            ('profile3-runtime2', envelope(sem_of(1, 3, 3, 2, 1, p3, digest))),
            ('profile6-u64', envelope(sem_of(1, 3, 6, 1, 2, p6u, digest))),
            ('profile6-view-ops-6-7-8', envelope(sem_of(1, 3, 6, 2, 2, p6v, digest))),
            ('profile6-write-alias-255-on-view-alias-1', envelope(sem_of(1, 3, 6, 2, 2, p6v[:4] + [
                rec(3, 8, 1, 4, 19, 255, 24, 0, 1, imm=3)], digest))),
            ('profile1-division', envelope(sem_of(1, 1, 1, 0, 1, d1, digest))),
            ('profile7-division', envelope(sem_of(2, 5, 7, 0, 1, d7, digest)))]


def random_mutations(envs, rng, count):
    out = []
    for j in range(count):
        base = rng.choice(envs)
        kind = rng.choice(['raw', 'resealed', 'semantic-only', 'header'])
        if kind == 'semantic-only':
            base = envelope(split(base)[0])
        b = bytearray(base)
        if kind == 'header':
            pos = [rng.randrange(0, min(len(b), 57))]
        else:
            pos = [rng.randrange(0, len(b)) for _ in range(rng.choice([1, 1, 2, 3]))]
        for p in pos:
            b[p] = rng.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 255, rng.randrange(256), b[p] ^ (1 << rng.randrange(8))])
        b = bytes(b)
        if kind == 'resealed':
            b = reseal(b)
        out.append(('random-%05d-%s' % (j, kind), b))
    return out


# ---------------------------------------------------------------- differential runner

def run(argv, data):
    r = subprocess.run([str(x) for x in argv], input=data, capture_output=True, timeout=30)
    return r.returncode, r.stdout


def collect_imports(args):
    """Every F1 import plus every F2 import of each importer present: (label, envelope)."""
    out = []
    f1 = json.loads((ROOT / 'tests/universal/translation/fixtures-f1.json').read_text())
    tools = Path(args.tools)
    for p in f1['positives']:
        rc, env = run([tools / 'c-import'], p['source'].encode())
        if rc == 0:
            out.append(('import-c-' + p['id'], env))
        else:
            out.append(('import-c-FAILED-' + p['id'], None))
    for lang in ('rust', 'python', 'cpp', 'zig', 'go'):
        path = ROOT / ('tests/universal/translation/fixtures-f2-%s.json' % lang)
        imp = tools / ('%s-import' % lang)
        if not path.is_file() or not imp.is_file():
            continue
        d = json.loads(path.read_text())
        for p in d['positives'] + d.get('extra_positives', []) + d.get('controls', []):
            rc, env = run([imp], p['source'].encode())
            out.append(('import-%s-%s' % (lang, p['id']), env if rc == 0 else None))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--sa', required=True, help='native v2 codec binary (build/L832/sa)')
    ap.add_argument('--sa-v1', help='frozen v1 codec binary (build/L574/sa): must refuse every v2 unit 66')
    ap.add_argument('--tools', default=str(ROOT / 'tools/adapters'), help='directory of the public importers')
    ap.add_argument('--random', type=int, default=4000)
    ap.add_argument('--seed', default='623-v2-reference')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = Path(a.out).resolve()
    out.mkdir(parents=True, exist_ok=False)
    ev = {'schema': 'omega.universal.source-aware.v2-reference.v1', 'spec': {'path': SPEC[0], 'sha256': SPEC[1]},
          'reference_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          'legality_mode_6': 'NOT_RUN: the per-operation reply is not specified byte for byte'}
    spec = ROOT / SPEC[0]
    if not spec.is_file() or hashlib.sha256(spec.read_bytes()).hexdigest() != SPEC[1] or not Path(a.sa).is_file():
        ev['result'] = 'NOT_RUN'
        (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
        print('NOT_RUN spec pin or codec absent')
        return 3
    ev['sa_sha256'] = hashlib.sha256(Path(a.sa).read_bytes()).hexdigest()
    ref = Ref(semantics_digest())
    imports = collect_imports(a)
    ev['imports'] = {'total': len(imports), 'failed': [l for l, e in imports if e is None]}
    valid = [(l, e) for l, e in imports if e is not None]
    cases = list(valid)
    for l, e in valid:
        sem, _ = split(e)
        cases += [('%s/R-%s' % (l, m), envelope(R(sem, m))) for m in MEMBERS]
    seen = set()
    # corner sweep on the F1-P05 import of each importer present (its own member, with its own sidecar);
    # every value of every version byte on the C and Go imports
    for lang in MEMBERS:
        pick = next(((l, e) for l, e in valid if l.startswith('import-%s-' % lang) and l.endswith('P05')), None)
        if pick:
            cases += [('%s/%s' % (pick[0], n), b) for n, b in corner_cases(pick[1], lang in ('c', 'go'))]
    # valid profile 3 and 6 units (ops 6/7/8) and division, checked by the section 5 step 6 rows
    built = constructed_units(semantics_digest())
    cases += [('constructed-%s' % n, e) for n, e in built]
    rng = random.Random(a.seed)
    cases += random_mutations([e for _, e in valid] + [envelope(R(split(e)[0], m)) for _, e in valid for m in MEMBERS],
                              rng, a.random)
    cases += [('constructed-' + n, b) for n, b in random_mutations([e for _, e in built], rng, a.random // 4)]
    base = valid[0][1]
    cases += [('mode-check', base)]
    agree, dis, by_status = 0, [], {}
    modes = [1, 2, 3, 4, 5]
    v1rows = {'total': 0, 'agree': 0, 'disagree': []}
    for label, b in cases:
        # every mode for a unit the reference accepts; for a refused unit decode (2) and replay (5),
        # whose status is the shared decode status
        accepted = ref.decode(b)[0] == 0
        for mode in ([0, 7, 9, 255] if label == 'mode-check' else modes if accepted else [2, 5]):
            want = ref.perform(mode, b)
            got = run([a.sa], bytes([mode]) + b)
            if got == want:
                agree += 1
                by_status[want[0]] = by_status.get(want[0], 0) + 1
            else:
                dis.append({'case': label, 'mode': mode, 'reference': [want[0], want[1].hex()[:96]],
                            'native': [got[0], got[1].hex()[:96]], 'envelope_hex': b.hex()})
        if a.sa_v1 and len(b) > 17 and b[:5] == b'OMSB\x01' and ref.decode(b)[0] == 0 and b[17] == 2:
            v1rows['total'] += 1
            got = run([a.sa_v1], b'\x02' + b)
            if got == (66, b''):
                v1rows['agree'] += 1
            else:
                v1rows['disagree'].append({'case': label, 'status': got[0], 'stdout_bytes': len(got[1])})
    ev.update({'cases': len(cases), 'comparisons': agree + len(dis), 'agree': agree, 'disagree': len(dis),
               'agreements_by_status': {str(k): v for k, v in sorted(by_status.items())},
               'v1_codec_refuses_v2_66': v1rows if a.sa_v1 else 'NOT_RUN', 'disagreements': dis[:400]})
    v1ok = not a.sa_v1 or not v1rows['disagree']
    ev['result'] = 'FAIL' if dis or not v1ok else 'PASS'
    (out / 'evidence.json').write_text(json.dumps(ev, indent=1) + '\n')
    print(json.dumps({k: ev[k] for k in ('result', 'cases', 'comparisons', 'agree', 'disagree',
                                         'agreements_by_status')}, indent=1))
    if a.sa_v1:
        print('v1 codec refuses v2 with 66: %d/%d' % (v1rows['agree'], v1rows['total']))
    for d in dis[:20]:
        print('DISAGREE', d['case'], 'mode', d['mode'], 'ref', d['reference'][0], 'native', d['native'][0])
    return 1 if ev['result'] == 'FAIL' else 0


if __name__ == '__main__':
    raise SystemExit(main())
