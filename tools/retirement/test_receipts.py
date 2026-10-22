#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""TEST-ONLY production receipts, created in a temporary directory and never published.
Exercise the real assessor with correctly hashed contents, including the parent's spoof.
"""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
from policy import ROOT, STAGES, assess

E = ROOT / 'evidence/seed-retirement'
BASE = json.loads((E / 'retirement-manifest.json').read_text())

def trn(m):
    return next(a for a in m['artifacts'] if a['id'] == 'aien-replay-trn1-schema1')

def promote(m):
    a = trn(m)
    a.update(live_legacy=False, production_retired=True, production_cutover=True)
    a['dependency_closure']['status'] = 'PRODUCTION_VERIFIED'
    a['gate'] = dict.fromkeys(STAGES, 'PASS')
    m['counts']['legacy_artifact_rows'] -= 1
    m['counts']['verified_production_retirements'] += 1
    return a

def reference(path, scope='production'):
    return {'path': str(path.relative_to(ROOT)),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'scope': scope}

def rejected(name, m, contains=None):
    r = assess(m)
    assert r['status'] == 'INVALID' and r['errors'], (name, r)
    if contains:
        assert any(contains in e for e in r['errors']), (name, r)
    print(json.dumps({'case': name, 'status': 'REJECTED', 'errors': r['errors']}))

# Exact reviewed/reproduced spoof: every label and count is production-consistent,
# but the correctly hashed deployed receipt explicitly says isolated/cutover false.
spoof = copy.deepcopy(BASE)
a = promote(spoof)
a['gate_evidence'] = {s: reference(E / 'deployment.json') for s in STAGES}
rejected('relabel existing isolated deployment as all production stages', spoof)

DETAILS = {
    'executable_equivalence': {'cases_run': 3, 'cases_passed': 3},
    'api_abi_data': {'api_compatible': True, 'abi_compatible': True, 'data_compatible': True},
    'isolated_candidate': {'candidate_smoke': True},
    'coordinator_model_train': {'coordinator': True, 'model': True, 'train': True},
    'dependency_closure': {'status': 'PRODUCTION_VERIFIED', 'unresolved': []},
    'transactional_swap_rollback': {'swap': True, 'rollback': True},
    'main_archive_smoke': {'archived': True, 'main_smoke': True},
}
with tempfile.TemporaryDirectory(prefix='TEST-ONLY-receipts-', dir=E) as tmp:
    directory = Path(tmp)
    good = copy.deepcopy(BASE)
    a = promote(good)
    identity = {k: a[k] for k in ('source_commit', 'source_sha256', 'replacement_path',
                                'replacement_source_sha256', 'seed_sha256')}
    identity.update(artifact_id=a['id'], native_sha256=a['candidate']['sha256'])
    receipts = {}
    for stage in STAGES:
        receipt = {'schema': 1, 'receipt_type': 'omega.retirement.stage',
                   'stage': stage, 'scope': 'production', 'status': 'PASS',
                   'production_cutover': True, 'identity': identity.copy(),
                   'details': DETAILS[stage]}
        path = directory / (stage + '.json')
        path.write_text(json.dumps(receipt))
        receipts[stage] = receipt
        a['gate_evidence'][stage] = reference(path)
    report = assess(good)
    assert report['status'] == 'PARTIAL' and report['errors'] == [], report
    assert report['computed_counts']['verified_production_retirements'] == 1, report
    print(json.dumps({'case': 'TEST-ONLY consistent production stage receipts', 'status': 'PASS'}))

    def change_receipt(name, change, stage=STAGES[0], raw=None):
        bad = copy.deepcopy(good)
        receipt = copy.deepcopy(receipts[stage])
        change(receipt)
        path = directory / 'negative.json'
        path.write_text(json.dumps(receipt) if raw is None else raw)
        trn(bad)['gate_evidence'][stage] = reference(path)
        rejected(name, bad)

    for key, value in [('artifact_id', 'other-artifact'), ('source_commit', '0'*40),
                       ('source_sha256', '0'*64), ('replacement_source_sha256', '0'*64),
                       ('native_sha256', '0'*64), ('seed_sha256', '0'*64),
                       ('replacement_path', 'seed/math/core.om')]:
        change_receipt('wrong identity ' + key, lambda r, k=key, v=value: r['identity'].update({k: v}))
    change_receipt('original Rust hash substituted for Omega source hash',
                   lambda r: r['identity'].update(replacement_source_sha256=identity['source_sha256']))
    for key, value in [('schema', 2), ('receipt_type', 'unknown'), ('stage', STAGES[1]),
                       ('scope', 'isolated'), ('production_cutover', False),
                       ('production_cutover', 1), ('status', 'NOT_RUN'), ('identity', []),
                       ('details', {}), ('details', {'cases_run': 3, 'cases_passed': 2})]:
        change_receipt('invalid receipt ' + key + ' ' + str(value), lambda r, k=key, v=value: r.update({k: v}))
    for key in ('receipt_type', 'stage', 'identity', 'scope', 'production_cutover'):
        change_receipt('missing receipt ' + key, lambda r, k=key: r.pop(k))
    for raw in ('{', '[]', 'null', '"receipt"', '{"scope":NaN}'):
        change_receipt('malformed receipt ' + raw, lambda r: None, raw=raw)
    change_receipt('duplicate JSON scope', lambda r: None,
                   raw=json.dumps(receipts[STAGES[0]])[:-1] + ',"scope":"isolated"}')
    for stage in STAGES[1:]:
        bad = copy.deepcopy(good)
        trn(bad)['gate_evidence'][stage] = copy.deepcopy(trn(good)['gate_evidence'][STAGES[0]])
        rejected('reuse executable receipt for ' + stage, bad)
    for stage in STAGES:
        change_receipt('missing stage detail ' + stage, lambda r: r.update(details={}), stage)
    for key in ('replacement_source_sha256', 'seed_sha256', 'candidate'):
        bad = copy.deepcopy(good)
        trn(bad).pop(key)
        rejected('missing manifest identity ' + key, bad)
    bad = copy.deepcopy(good)
    trn(bad)['replacement_source_sha256'] = '0'*64
    for stage in STAGES:
        receipt = copy.deepcopy(receipts[stage])
        receipt['identity']['replacement_source_sha256'] = '0'*64
        path = directory / ('coherent-wrong-' + stage + '.json')
        path.write_text(json.dumps(receipt))
        trn(bad)['gate_evidence'][stage] = reference(path)
    rejected('coherent false Omega source hash versus actual source', bad)
    for key, value in [('production_retired', 1), ('production_cutover', 'true'), ('live_legacy', 0)]:
        bad = copy.deepcopy(good)
        trn(bad)[key] = value
        rejected('nonboolean production flag ' + key, bad)
    # Rehashing wrong isolated contents must not make them valid evidence either.
    for key in ('source_sha256', 'native_sha256'):
        bad = copy.deepcopy(BASE)
        receipt = json.loads((E / 'trn1-summary.json').read_text())
        receipt[key] = '0'*64
        path = directory / 'wrong-isolated.json'
        path.write_text(json.dumps(receipt))
        trn(bad)['gate_evidence'][STAGES[0]] = reference(path, 'isolated')
        rejected('rehashed isolated wrong ' + key, bad)
    bad = copy.deepcopy(BASE)
    trn(bad)['gate_evidence']['dependency_closure'] = copy.deepcopy(trn(bad)['gate_evidence'][STAGES[0]])
    rejected('isolated equivalence reused for dependency closure', bad)
    for path in ('../deployment.json', '/etc/passwd', 'evidence/seed-retirement/missing.json'):
        bad = copy.deepcopy(good)
        trn(bad)['gate_evidence'][STAGES[0]]['path'] = path
        rejected('invalid evidence path ' + path, bad)
    outside = ROOT / 'seed/math/CONTRACT.md'
    link = directory / 'outside.json'
    link.symlink_to(outside)
    bad = copy.deepcopy(good)
    trn(bad)['gate_evidence'][STAGES[0]] = reference(link)
    rejected('evidence symlink escapes task directory', bad)
    # NOT_APPLICABLE still needs a matching stage, scope, identity, and explanation.
    stage = STAGES[1]
    bad = copy.deepcopy(good)
    trn(bad)['gate'][stage] = 'NOT_APPLICABLE'
    rejected('not-applicable gate cannot reuse a PASS receipt', bad)
    receipt = copy.deepcopy(receipts[stage])
    receipt.update(status='NOT_APPLICABLE', reason='TEST-ONLY documented nonapplicability')
    path = directory / 'not-applicable.json'
    path.write_text(json.dumps(receipt))
    trn(bad)['gate_evidence'][stage] = reference(path)
    assert assess(bad)['errors'] == [], assess(bad)

assert assess(BASE)['status'] == 'PARTIAL' and assess(BASE)['errors'] == []
assert assess(BASE)['computed_counts']['verified_production_retirements'] == 0
print('RECEIPTS PASS; synthetic production fixtures removed; actual retirement count remains zero')
