#!/usr/bin/env python3
"""Fixture set F1 (spec/universal/translation-v1.md section 8): the exact sources of the reviewed C import
gate, identified by SHA256. `--check` fails if fixtures-f1.json no longer matches c_import_gate.py."""
import hashlib, importlib.util, json, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('gate', ROOT / 'tests/universal/c_import_gate.py')
g = importlib.util.module_from_spec(spec); spec.loader.exec_module(g)
pos = [{'id': 'F1-P%02d' % i, 'sha256': hashlib.sha256(t.encode()).hexdigest(), 'name': n, 'inputs': k,
        'source': t} for i, (t, n, k) in enumerate(g.POSITIVES)]
neg = [{'id': 'F1-N%02d' % i, 'sha256': hashlib.sha256(b).hexdigest(), 'bytes': len(b)} for i, b in enumerate(g.NEGATIVES)]
doc = {'schema': 'omega.universal.fixtures.v1', 'set': 'F1', 'contract': 'spec/universal/translation-v1.md',
       'from': 'tests/universal/c_import_gate.py', 'positives': pos, 'refusals': neg}
out = ROOT / 'tests/universal/translation/fixtures-f1.json'
text = json.dumps(doc, indent=1, ensure_ascii=False) + '\n'
if sys.argv[1:] == ['--check']:
    ok = out.read_text() == text
    print('F1 fixtures %s: %d positives, %d refusals' % ('match' if ok else 'DIFFER', len(pos), len(neg)))
    sys.exit(0 if ok else 1)
out.write_text(text); print('wrote', out, len(pos), 'positives', len(neg), 'refusals')
