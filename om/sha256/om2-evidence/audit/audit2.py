#!/usr/bin/env python3
# L838 audit part 2: overlap, use-after-final, whole-context copy/compare, field access.
# Heuristic flags; every flag is reviewed by hand (audit.md records the verdicts).
import re, json, sys, subprocess, collections
sys.path.insert(0, '/tmp/zl/sha-audit')
from audit import strip_comments, args_at, norm, base, ROOT  # noqa: re-runs inventory (cheap)
calls = [c for c in json.load(open('/tmp/zl/sha-audit/calls.json')) if not c['defn']]
built = [c for c in calls if not c['file'].startswith('evidence/')]
src = {}
def text(f):
    if f not in src:
        src[f] = strip_comments(open(f'{ROOT}/{f}', encoding='utf-8', errors='replace').read())
    return src[f]

def ctxkey(a):
    a = norm(a)
    a = re.sub(r'^&\s*', '', a)
    return re.sub(r'\s', '', a)

print('== A. sha256_hash: digest base == data base, or digest derived from data')
for c in built:
    if c['fn'] != 'hash' or len(c['args']) != 3: continue
    d, n, o = c['args']
    bd, bo = base(d), base(o)
    same = bd == bo or bd.split('->')[0].split('.')[0] == bo.split('->')[0].split('.')[0]
    if same:
        print(f"  {c['file']}:{c['line']}: hash({norm(d)}, {norm(n)}, {norm(o)})")

print('== B. sha256_final: digest arg mentions the ctx expression')
for c in built:
    if c['fn'] != 'final' or len(c['args']) != 2: continue
    k, o = ctxkey(c['args'][0]), c['args'][1]
    root = re.split(r'->|\.', k)[0]
    if root and re.search(r'\b' + re.escape(root) + r'\b', o):
        print(f"  {c['file']}:{c['line']}: final({c['args'][0]}, {norm(o)})")

print('== C. sha256_update: data arg mentions the ctx expression')
for c in built:
    if c['fn'] != 'update' or len(c['args']) != 3: continue
    k, d = ctxkey(c['args'][0]), c['args'][1]
    root = re.split(r'->|\.', k)[0]
    if root and re.search(r'\b' + re.escape(root) + r'\b', d):
        print(f"  {c['file']}:{c['line']}: update({c['args'][0]}, {norm(d)}, ...)")

# Function extents: top-level brace blocks.
def functions(s):
    out = []; depth = 0; start = None
    for i, ch in enumerate(s):
        if ch == '{':
            if depth == 0: start = i
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0 and start is not None: out.append((start, i)); start = None
    return out

LOOP = re.compile(r'\b(for|while|do)\b')
def loop_spans(s, a, b):
    """spans (open, close) of loop bodies inside s[a:b] (braced bodies only, plus single-statement for/while)."""
    spans = []
    for m in LOOP.finditer(s, a, b):
        i = m.end()
        if m.group(1) != 'do':
            while i < b and s[i].isspace(): i += 1
            if i >= b or s[i] != '(': continue
            _, i = args_at(s, i); i += 1
        while i < b and s[i].isspace(): i += 1
        if i < b and s[i] == '{':
            d = 0
            for j in range(i, b + 1):
                if s[j] == '{': d += 1
                elif s[j] == '}':
                    d -= 1
                    if d == 0: spans.append((i, j)); break
        else:
            j = s.find(';', i); spans.append((i, j))
    return spans

print('== D. lifecycle: update/final on a ctx after its final with no init between (textual), or final in a loop whose body has no init of that ctx')
by_file = collections.defaultdict(list)
for c in built: by_file[c['file']].append(c)
ctx_param_final = []
for f, cs in sorted(by_file.items()):
    s = text(f)
    for (a, b) in functions(s):
        inside = [c for c in cs if a <= c['off'] <= b]
        if not inside: continue
        seq = collections.defaultdict(list)
        for c in sorted(inside, key=lambda c: c['off']):
            if c['fn'] == 'hash': continue
            seq[ctxkey(c['args'][0])].append(c)
        loops = loop_spans(s, a, b)
        for k, ops in seq.items():
            st = None
            for c in ops:
                if c['fn'] == 'init': st = 'live'
                elif c['fn'] == 'final':
                    if st == 'done': print(f"  {f}:{c['line']}: final({k}) again after final")
                    st = 'done'
                elif c['fn'] == 'update' and st == 'done':
                    print(f"  {f}:{c['line']}: update({k}) after final")
            inits = [c for c in ops if c['fn'] == 'init']
            finals = [c for c in ops if c['fn'] == 'final']
            for fc in finals:
                for (la, lb) in loops:
                    if la <= fc['off'] <= lb and not any(la <= ic['off'] <= lb for ic in inits):
                        print(f"  {f}:{fc['line']}: final({k}) inside loop body at offset {la}..{lb} without init({k}) in that loop")
                        break
            if finals and not inits:
                ctx_param_final.append(f"{f}:{finals[0]['line']}: final({k}) with no init({k}) in the same function")
print('== D2. finals in functions that do not init that ctx (wrappers; callers reviewed by hand)')
for x in ctx_param_final: print('  ' + x)

print('== E. sha256_ctx object names, struct members, copies/compares')
files = [f for f in subprocess.run(['git', '-C', ROOT, 'ls-files', '--', '*.c', '*.h', '*.cc', '*.cpp', '*.inc'],
         capture_output=True, text=True).stdout.split() if not f.startswith('evidence/')]
decl = re.compile(r'\bsha256_ctx\s+(\*\s*)?([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*([=;,)\[])')
names = collections.defaultdict(set); ptrs = collections.defaultdict(set)
structs_with_ctx = []
for f in files:
    s = text(f)
    if 'sha256_ctx' not in s: continue
    for m in decl.finditer(s):
        (ptrs if m.group(1) else names)[f].add(m.group(2))
        line = s.count('\n', 0, m.start()) + 1
        if not m.group(1) and m.group(4) == '=':
            rest = s[m.end():s.find(';', m.end())]
            print(f"  copy-init? {f}:{line}: sha256_ctx {m.group(2)} = {rest.strip()[:80]}")
    for m in re.finditer(r'\bsha256_ctx\s+[A-Za-z_]\w*\s*(\[[^\]]*\])?\s*;', s):
        # member if inside a struct { } ... ; check nearest unmatched '{' is preceded by struct
        i = m.start(); d = 0
        while i > 0:
            i -= 1
            if s[i] == '}': d += 1
            elif s[i] == '{':
                if d == 0: break
                d -= 1
        head = s[max(0, i - 60):i]
        if re.search(r'\b(struct|union)\b[^;{}]*$', head) or re.search(r'typedef\s+struct', head):
            line = s.count('\n', 0, m.start()) + 1
            structs_with_ctx.append(f"{f}:{line}: member {m.group(0).strip()}")
print('  struct members of type sha256_ctx:')
for x in structs_with_ctx: print('    ' + x)
print('  ctx by-value names per file:', sum(len(v) for v in names.values()), ' pointer names:', sum(len(v) for v in ptrs.values()))
for f in files:
    s = text(f)
    if 'sha256_ctx' not in s and not names.get(f): continue
    nm = names.get(f, set()) | ptrs.get(f, set())
    for m in re.finditer(r'\b(memcpy|memmove|memcmp|bcmp|memset|explicit_bzero)\s*\(', s):
        args, _ = args_at(s, m.end() - 1)
        joined = ' '.join(args)
        if 'sha256_ctx' in joined or any(re.search(r'(&|\*)\s*' + re.escape(n) + r'\b(?!\s*(\.|->))', joined) or
                                         re.search(r'sizeof\s*\(?\s*\*?' + re.escape(n) + r'\b\s*\)?(?!\s*(\.|->))', joined) for n in nm):
            line = s.count('\n', 0, m.start()) + 1
            print(f"  {m.group(1)} {f}:{line}: {m.group(1)}({', '.join(norm(x) for x in args)})")
    for n in nm:
        for m in re.finditer(r'(\*\s*)?\b' + re.escape(n) + r'\s*=(?!=)', s):
            line = s.count('\n', 0, m.start()) + 1
            ctxline = s[s.rfind('\n', 0, m.start()) + 1:s.find('\n', m.start())]
            if re.search(r'\bsha256_ctx\b', ctxline) and not m.group(1): continue  # declaration handled above
            if n in ptrs.get(f, set()) and not m.group(1): continue  # pointer rebinding
            print(f"  assign {f}:{line}: {ctxline.strip()[:120]}")
        for m in re.finditer(r'\b' + re.escape(n) + r'\s*(\.|->)\s*(state|count|buffer)\b', s):
            line = s.count('\n', 0, m.start()) + 1
            print(f"  field {f}:{line}: {n}{m.group(1)}{m.group(2)}")
print('== F. field access on any expression (.state/.count/.buffer near sha256_ctx use), by-value params/returns')
for f in files:
    s = text(f)
    if 'sha256_ctx' not in s: continue
    for m in re.finditer(r'\bsha256_ctx\s+[A-Za-z_]\w*\s*[,)]', s):
        pre = s[max(0, m.start() - 200):m.start()]
        if re.search(r'\(\s*[^;{}]*$', pre):
            line = s.count('\n', 0, m.start()) + 1
            print(f"  by-value param {f}:{line}: {m.group(0)}")
    for m in re.finditer(r'(^|\n)\s*(static\s+)?(inline\s+)?sha256_ctx\s+[A-Za-z_]\w*\s*\(', s):
        line = s.count('\n', 0, m.start()) + 2
        print(f"  returns by value {f}:{line}")
