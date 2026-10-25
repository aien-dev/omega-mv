#!/usr/bin/env python3
# L838 audit part 3: one level of wrappers that forward a parameter as sha256_hash data and another as
# the digest; every call of such a wrapper whose two arguments share a base is listed for review.
import re, json, sys, subprocess, collections
sys.argv = [sys.argv[0], sys.argv[1]]
from audit import strip_comments, args_at, norm, base, ROOT
calls = [c for c in json.load(open('/tmp/zl/sha-audit/calls.json')) if not c['defn'] and not c['file'].startswith('evidence/')]
files = [f for f in subprocess.run(['git', '-C', ROOT, 'ls-files', '--', '*.c', '*.h'], capture_output=True, text=True).stdout.split()
         if re.match(r'(src|tools|tests|research|om)/', f)]
txt = {f: strip_comments(open(f'{ROOT}/{f}', errors='replace').read()) for f in files}
wrappers = {}
HDR = re.compile(r'([A-Za-z_]\w*)\s*\(([^;{}()]*)\)\s*\{')
for f, s in txt.items():
    for m in HDR.finditer(s):
        name = m.group(1)
        if name in ('if', 'for', 'while', 'switch', 'return', 'sizeof'): continue
        params = [re.findall(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*$', p.strip()) for p in m.group(2).split(',')]
        params = [p[0][0] if p else None for p in params]
        # body extent
        i = m.end() - 1; d = 0
        for j in range(i, len(s)):
            if s[j] == '{': d += 1
            elif s[j] == '}':
                d -= 1
                if d == 0: break
        body = s[i:j]
        for h in re.finditer(r'\bsha256_hash\s*\(', body):
            a, _ = args_at(body, h.end() - 1)
            if len(a) != 3: continue
            bd, bo = base(a[0]), base(a[2])
            if bd in params and bo in params and bd != bo:
                wrappers[name] = (f, params.index(bd), params.index(bo))
print('wrappers forwarding (data, digest) params into sha256_hash:', len(wrappers))
for w, (f, di, oi) in sorted(wrappers.items()):
    print(f'  {w} ({f}) data=arg{di} digest=arg{oi}')
    for f2, s in txt.items():
        for m in re.finditer(r'\b' + re.escape(w) + r'\s*\(', s):
            a, end = args_at(s, m.end() - 1)
            if len(a) <= max(di, oi): continue
            if s[end + 1:end + 20].lstrip().startswith('{'): continue
            bd, bo = base(a[di]), base(a[oi])
            if bd.split('->')[0].split('.')[0] == bo.split('->')[0].split('.')[0]:
                line = s.count('\n', 0, m.start()) + 1
                print(f'    SAME-BASE {f2}:{line}: {w}({", ".join(norm(x) for x in a)})')
