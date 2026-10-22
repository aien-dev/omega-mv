#!/usr/bin/env python3
"""Negative controls for retirement claims; never alters live evidence."""
import copy
import json
from pathlib import Path
import subprocess
import sys
from policy import assess,ROOT

E=ROOT/'evidence/seed-retirement'
m=json.loads((E/'retirement-manifest.json').read_text())
assert assess(m)['status']=='PARTIAL' and not assess(m)['errors']
results=[{'case':'honest partial inventory','status':'PASS'}]

def reject(name,change):
    bad=copy.deepcopy(m);change(bad)
    report=assess(bad)
    assert report['status']=='INVALID',(name,report)
    results.append({'case':name,'status':'REJECTED','errors':report['errors']})

def trn(m):return next(a for a in m['artifacts'] if a['id']=='aien-replay-trn1-schema1')
reject('whole system claim with live legacy',lambda x:x.update(status='RETIRED'))
reject('invented retirement count',lambda x:x['counts'].update(verified_production_retirements=1))
reject('isolated candidate called retired',lambda x:trn(x).update(production_retired=True))
reject('missing evidence',lambda x:trn(x).update(gate_evidence={}))
reject('changed evidence hash',lambda x:trn(x)['gate_evidence']['executable_equivalence'].update(sha256='0'*64))
reject('changed stage list',lambda x:x.update(required_cutover_stages=[]))
reject('empty artifact list',lambda x:x.update(artifacts=[]))
reject('missing stage',lambda x:trn(x)['gate'].pop('api_abi_data'))
reject('duplicate artifact id',lambda x:x['artifacts'].append(copy.deepcopy(x['artifacts'][0])))
# Spoof all stage labels PASS while retaining isolated receipts and no production proof.
def forged(x):
    a=trn(x);ev=a['gate_evidence']['executable_equivalence']
    for stage in x['required_cutover_stages']:
        a['gate'][stage]='PASS';a['gate_evidence'][stage]=ev.copy()
    a.update(live_legacy=False,production_retired=True,production_cutover=True)
    x['counts']['legacy_artifact_rows']-=1;x['counts']['verified_production_retirements']+=1
reject('passing isolated evidence substituted for production evidence',forged)
p=subprocess.run([sys.executable,str(ROOT/'tools/retirement/policy.py'),'--require-retired'],capture_output=True)
assert p.returncode==1
results.append({'case':'require-retired fails closed on actual inventory','exit':p.returncode})
(E/'policy-tests.json').write_text(json.dumps({'status':'PASS','cases':results},indent=2)+'\n')
print(f'POLICY PASS cases={len(results)}')
