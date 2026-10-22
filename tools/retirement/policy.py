#!/usr/bin/env python3
"""Fail-closed retirement reporting. PASS for an isolated port is not retirement.
Exit 0: consistent manifest (possibly PARTIAL); 1: --require-retired unmet;
2: inconsistent/unsupported claim or malformed/missing evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
STAGES=('executable_equivalence','api_abi_data','isolated_candidate','coordinator_model_train','dependency_closure','transactional_swap_rollback','main_archive_smoke')


def assess(m):
    errors=[];blockers=[]
    if m.get('schema')!=1:errors.append('unsupported schema')
    if m.get('status') not in ('PARTIAL','RETIRED'):errors.append('unsupported status')
    if m.get('required_cutover_stages')!=list(STAGES):errors.append('cutover stages missing or changed')
    if not m.get('coverage',{}).get('complete'):blockers.append('inventory coverage incomplete')
    artifacts=m.get('artifacts',[])
    if not artifacts:errors.append('artifact inventory empty')
    ids=[a.get('id') for a in artifacts]
    if None in ids or len(set(ids))!=len(ids):errors.append('artifact ids missing or duplicated')
    for a in artifacts:
        name=a.get('id','?')
        retired=a.get('production_retired',False)
        if a.get('live_legacy',True):blockers.append(name+': legacy remains')
        gates=a.get('gate',{})
        for stage in STAGES:
            value=gates.get(stage)
            if value not in ('PASS','NOT_RUN','BLOCKED','NOT_APPLICABLE'):errors.append(name+': invalid gate '+stage)
            if value in ('PASS','NOT_APPLICABLE'):
                ev=a.get('gate_evidence',{}).get(stage)
                if not ev:
                    errors.append(name+': evidence missing for '+stage);continue
                path=ROOT/ev.get('path','')
                if not path.resolve().is_relative_to(ROOT/'evidence/seed-retirement'):
                    errors.append(name+': evidence outside task directory');continue
                if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=ev.get('sha256'):
                    errors.append(name+': missing or changed evidence for '+stage)
                if retired and ev.get('scope')!='production':errors.append(name+': isolated evidence cannot retire production')
        if retired:
            if a.get('live_legacy',True):errors.append(name+': retired while legacy remains')
            if not a.get('production_cutover',False):errors.append(name+': production cutover unproved')
            if not a.get('source_commit') or not a.get('source_sha256'):errors.append(name+': source provenance unresolved')
            if not a.get('replacement_path') or a.get('replacement_language')!='Omega':errors.append(name+': native Omega replacement absent')
            if a.get('replacement_birth')!='verified seed chain':errors.append(name+': replacement birth unproved')
            if a.get('dependency_closure',{}).get('status')!='PRODUCTION_VERIFIED':errors.append(name+': production dependency closure unproved')
            if any(gates.get(s) not in ('PASS','NOT_APPLICABLE') for s in STAGES):errors.append(name+': cutover gates unfinished')
        elif a.get('retirement_target',True):
            blockers.append(name+': production retirement unproved')
    counts=m.get('counts',{})
    computed={
        'observed_installed_or_running_elf':sum('observation' in a for a in artifacts),
        'observed_running_elf':sum(bool(a.get('observation',{}).get('pids',[])) for a in artifacts),
        'legacy_artifact_rows':sum(a.get('live_legacy',True) for a in artifacts),
        'verified_production_retirements':sum(a.get('production_retired',False) for a in artifacts),
    }
    for name,value in computed.items():
        if counts.get(name)!=value:errors.append('count mismatch: '+name)
    if m.get('status')=='RETIRED' and blockers:errors.append('RETIRED claim with remaining blockers')
    return {'status':'INVALID' if errors else ('PARTIAL' if blockers else 'RETIRED'),'errors':errors,'blockers':blockers,'computed_counts':computed}


def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,default=ROOT/'evidence/seed-retirement/retirement-manifest.json');p.add_argument('--require-retired',action='store_true');args=p.parse_args()
    try:report=assess(json.loads(args.manifest.read_text()))
    except (OSError,ValueError,KeyError,TypeError) as e:
        print(json.dumps({'status':'INVALID','error':str(e)}));return 2
    print(json.dumps(report,indent=2))
    if report['errors']:return 2
    return int(args.require_retired and report['status']!='RETIRED')

if __name__=='__main__':sys.exit(main())
