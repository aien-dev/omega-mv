#!/usr/bin/env python3
# L838 audit (oracle tooling, outside the delivered closure): every caller of sha256_init/update/final/hash
# in files tracked by git and built from this repo (src tools tests research om; evidence/ holds non-built
# snapshots and is listed separately).
import re, subprocess, sys, collections, json
ROOT = sys.argv[1]
files = subprocess.run(['git', '-C', ROOT, 'ls-files', '--', '*.c', '*.h', '*.cc', '*.cpp', '*.inc'],
                       capture_output=True, text=True).stdout.split()
CALL = re.compile(r'\bsha256_(init|update|final|hash)\s*\(')

def strip_comments(s):
    # keep offsets: replace comment and string bodies by spaces (newlines kept)
    out = list(s); i = 0; n = len(s)
    while i < n:
        if s.startswith('/*', i):
            j = s.find('*/', i + 2); j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != '\n': out[k] = ' '
            i = j
        elif s.startswith('//', i):
            j = s.find('\n', i); j = n if j < 0 else j
            for k in range(i, j): out[k] = ' '
            i = j
        elif s[i] == '"' or s[i] == "'":
            q = s[i]; j = i + 1
            while j < n and s[j] != q:
                j += 2 if s[j] == '\\' else 1
            for k in range(i + 1, min(j, n)):
                if out[k] != '\n': out[k] = ' '
            i = j + 1
        else:
            i += 1
    return ''.join(out)

def args_at(s, p):  # s[p] == '('
    depth = 0; cur = []; args = []; i = p
    while i < len(s):
        c = s[i]
        if c in '([{':
            depth += 1
            if depth > 1: cur.append(c)
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                args.append(''.join(cur).strip()); return args, i
            cur.append(c)
        elif c == ',' and depth == 1:
            args.append(''.join(cur).strip()); cur = []
        else:
            cur.append(c)
        i += 1
    return args, i

def norm(a):
    a = re.sub(r'\s+', ' ', a)
    a = re.sub(r'\((const )?(uint8_t|unsigned char|char|void|uint32_t|uint64_t) ?\*\)', '', a)
    return a.strip()

def base(a):
    a = norm(a).lstrip('&*( ')
    m = re.match(r'[A-Za-z_][A-Za-z_0-9]*(\s*(->|\.)\s*[A-Za-z_][A-Za-z_0-9]*)*', a)
    return re.sub(r'\s', '', m.group(0)) if m else a

calls = []
for f in files:
    if not re.match(r'(src|tools|tests|research|om|evidence)/', f) and '/' in f:
        continue
    try:
        raw = open(f'{ROOT}/{f}', encoding='utf-8', errors='replace').read()
    except (IsADirectoryError, FileNotFoundError):
        continue
    s = strip_comments(raw)
    for m in CALL.finditer(s):
        # skip declarations/definitions: preceded by a type word on the same line
        line_start = s.rfind('\n', 0, m.start()) + 1
        prefix = s[line_start:m.start()]
        p = m.end() - 1
        args, end = args_at(s, p)
        after = s[end + 1:end + 40].lstrip()
        is_def = bool(re.match(r'^\s*((static|extern|inline)\s+)*void\s*$', prefix))
        if re.match(r'\s*#\s*define', prefix): is_def = True
        line = s.count('\n', 0, m.start()) + 1
        calls.append(dict(file=f, line=line, fn=m.group(1), args=args, defn=is_def, off=m.start()))

json.dump(calls, open('/tmp/zl/sha-audit/calls.json', 'w'), indent=0)
built = [c for c in calls if not c['defn'] and not c['file'].startswith('evidence/')]
snap = [c for c in calls if not c['defn'] and c['file'].startswith('evidence/')]
print('call sites (built dirs):', len(built), ' files:', len({c['file'] for c in built}))
print('by fn:', dict(collections.Counter(c['fn'] for c in built)))
print('by top dir:', dict(collections.Counter(c['file'].split('/')[0] for c in built)))
print('evidence snapshots (not built):', len(snap), 'in', len({c['file'] for c in snap}), 'files')
print('definitions/declarations skipped:', [(c['file'], c['line']) for c in calls if c['defn']])
