#!/usr/bin/env python3
"""Exact-code controls for extra C preprocessing (c-u64-import-v1: refusal 72).

Added after the #623 re-validation at c6316d4b found `#define`/`#include`/`#if` refused with 66
and `#pragma` with 69. Kept separate from the frozen c_import_controls.py, which the G1 validator pins.
"""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
HEADER='#include <stdint.h>\n'
ap=argparse.ArgumentParser();ap.add_argument('--importer',required=True);ap.add_argument('--out',required=True);a=ap.parse_args()
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False);exe=Path(a.importer).resolve()
refuse=[('define','#define X 1ULL\nuint64_t calc(uint64_t a){return a+X;}\n'),
        ('include','#include <stddef.h>\nuint64_t calc(uint64_t a){return a;}\n'),
        ('if','#if 1\nuint64_t calc(uint64_t a){return a;}\n#endif\n'),
        ('pragma','#pragma once\nuint64_t calc(uint64_t a){return a;}\n'),
        ('body-directive','uint64_t calc(uint64_t a){\n#define Y 2\nreturn a;}\n'),
        ('stringize-token','uint64_t calc(uint64_t a){return a # a;}\n')]
accept=[('hash-in-block-comment','/* #define X 1 */ uint64_t calc(uint64_t a){return a;}\n'),
        ('hash-in-line-comment','uint64_t calc(uint64_t a){return a; // #pragma once\n}\n')]
checks=[]
for name,body in refuse:
    r=subprocess.run([str(exe)],input=(HEADER+body).encode(),capture_output=True,timeout=10)
    ok=r.returncode==72 and r.stdout==b'' and r.stderr==b'C_IMPORT_UNSUPPORTED_PREPROCESSOR\n'
    checks.append({'name':name,'exit':r.returncode,'stderr':r.stderr.decode(errors='replace'),'result':'PASS' if ok else 'FAIL'})
for name,body in accept:
    r=subprocess.run([str(exe)],input=(HEADER+body).encode(),capture_output=True,timeout=10)
    ok=r.returncode==0 and r.stdout[:5]==b'OMSB\x01'
    checks.append({'name':name,'exit':r.returncode,'result':'PASS' if ok else 'FAIL'})
(out/'preproc-controls.json').write_text(json.dumps(checks,indent=1)+'\n')
bad=[c for c in checks if c['result']!='PASS']
for c in bad:print('FAIL',json.dumps(c))
if bad:raise SystemExit(1)
print('PASS preprocessing refusals=%d comment_hash_accepts=%d'%(len(refuse),len(accept)))
